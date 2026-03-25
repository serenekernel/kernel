#include <arch/hardware/fpu.h>
#include <arch/hardware/lapic.h>
#include <arch/internal/cpuid.h>
#include <arch/internal/cr.h>
#include <arch/internal/gdt.h>
#include <arch/interrupts.h>
#include <arch/msr.h>
#include <assert.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/dpc.h>
#include <common/fs/rdsk.h>
#include <common/fs/vfs.h>
#include <common/interrupts.h>
#include <common/io.h>
#include <common/ipi.h>
#include <common/irql.h>
#include <common/ldr/elf.h>
#include <common/ldr/elf_internal.h>
#include <common/ldr/sysv.h>
#include <common/requests.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/userspace/userspace.h>
#include <memory/heap.h>
#include <memory/memory.h>
#include <memory/pmm.h>
#include <memory/slab.h>
#include <memory/vmm.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <uacpi/uacpi.h>

void setup_protections() {
    arch_memory_barrier();
    uint64_t cr4 = __read_cr4();
    if(__cpuid_is_feature_supported(CPUID_FEATURE_UMIP)) {
        cr4 |= (1 << 11); // cr4.UMIP
        printf("UMIP: supported\n");
    } else {
        printf("UMIP: not supported\n");
    }

    if(__cpuid_is_feature_supported(CPUID_FEATURE_SMEP)) {
        cr4 |= (1 << 20); // cr4.SMEP
        printf("SMEP: supported\n");
    } else {
        printf("SMEP: not supported\n");
    }

    if(__cpuid_is_feature_supported(CPUID_FEATURE_SMAP)) {
        printf("SMAP: supported\n");
        cr4 |= (1 << 21); // cr4.SMAP
    } else {
        printf("SMAP: not supported\n");
    }

    uint64_t cr0 = __read_cr0();
    cr0 |= (1 << 16); // cr0.WP

    __write_cr0(cr0);
    __write_cr4(cr4);
    if(__cpuid_is_feature_supported(CPUID_FEATURE_UMIP)) {
        __asm__ volatile("clac"); // clear uap
    }

    arch_memory_barrier();
}

void setup_memory() {
    pmm_init();

    phys_addr_t highest_phys_address = 0;
    for(size_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_request.response->entries[i];
        if(entry->base + entry->length > highest_phys_address) { highest_phys_address = entry->base + entry->length; }
    }

    virt_addr_t virtual_start = (virt_addr_t) TO_HHDM(highest_phys_address);

    vmm_kernel_init(&kernel_allocator, virtual_start, virtual_start + 0x800000000);
    vm_paging_bsp_init(&kernel_allocator);

    vm_map_kernel();
    vm_address_space_switch(&kernel_allocator);
    slab_cache_init();
    init_heap();

    setup_protections();
}

void setup_acpi() {
    virt_addr_t temp_buffer = vmm_alloc_bytes(&kernel_allocator, 4096);
    uacpi_status ret = uacpi_setup_early_table_access((void*) temp_buffer, 4096);
    assertf(!uacpi_unlikely_error(ret), "uacpi_setup_early_table_access error: %s", uacpi_status_to_string(ret));
}

void setup_arch() {
    printf("CPU Vendor: %s\n", __cpuid_get_vendor_string());
    printf("CPU Name: %s\n", __cpuid_get_name_string());

    setup_gdt();
    dpc_init_queue();
    interrupts_setup_bsp();

    setup_acpi();
    lapic_init_bsp();
}

static uint32_t arch_ap_finished = 0;

void init_aps() {
    __atomic_store_n(&arch_ap_finished, 0, __ATOMIC_RELAXED);
    for(size_t i = 0; i < mp_request.response->cpu_count; i++) {
        if(mp_request.response->cpus[i]->lapic_id == lapic_get_id()) { continue; }

        printf("Starting AP with lapic id %u\n", mp_request.response->cpus[i]->lapic_id);
        atomic_store(&mp_request.response->cpus[i]->goto_address, &arch_init_ap);
        while(__atomic_load_n(&arch_ap_finished, __ATOMIC_RELAXED) == 0) { arch_pause(); }
    }
}

void arch_init_bsp() {
    init_cpu_local_bsp();

    setup_memory();
    setup_arch();

    interrupts_enable();
    assert(irql_get() == IRQL_PASSIVE);
    printf("Hello, %s!\n", arch_get_name());

    ipi_init_bsp();
    init_cpu_locals(mp_request.response->cpu_count);
    fpu_init_bsp();

    uint32_t current_core_id = 1;
    for(size_t i = 0; i < mp_request.response->cpu_count; i++) {
        printf("CPU %zu: lapic_id: %u processor_id %u\n", i, mp_request.response->cpus[i]->lapic_id, mp_request.response->cpus[i]->processor_id);
        if(mp_request.response->cpus[i]->lapic_id == lapic_get_id()) { continue; }
        mp_request.response->cpus[i]->extra_argument = current_core_id++;
    }

    userspace_init();
    // init_aps();
    sched_init_bsp();

    struct limine_file* initramfs_file = nullptr;
    for(size_t i = 0; i < module_request.response->module_count; i++) {
        if(strcmp(module_request.response->modules[i]->string, "initramfs-module") == 0) {
            initramfs_file = module_request.response->modules[i];
            break;
        }
    }

    assert(initramfs_file != nullptr && "initramfs.rdk not found");

    vfs_result_t res = vfs_mount(&fs_rdsk_ops, nullptr, (void*) initramfs_file->address);
    assertf(res == VFS_RESULT_OK, "Failed to mount initramfs  %d", res);

    printf("mounted initramfs\n");

    vfs_node_t* root_node;
    res = vfs_root(&root_node);
    assertf(res == VFS_RESULT_OK, "Failed to get root node %d", res);

    size_t offset = 0;
    while(1) {
        vfs_node_t* dirent;
        res = root_node->ops->readdir(root_node, &offset, &dirent);
        if(res == VFS_RESULT_ERR_NOT_FOUND) { break; }
        assertf(res == VFS_RESULT_OK, "Failed to readdir %d", res);
        if(dirent == nullptr) {
            // printf("dirent is null\n");
            break;
        }

        size_t name_size;
        res = dirent->ops->name(dirent, nullptr, 0, &name_size);
        assertf(res == VFS_RESULT_ERR_BUFFER_TOO_SMALL, "Failed to get dirent name size %d", res);
        char* name = heap_alloc(name_size);
        res = dirent->ops->name(dirent, name, name_size, nullptr);
        assertf(res == VFS_RESULT_OK, "Failed to get dirent name %d", res);
        vfs_node_attr_t attr;
        res = dirent->ops->attr(dirent, &attr);
        assertf(res == VFS_RESULT_OK, "Failed to get dirent attr %d", res);
        printf("%s: %s %d bytes\n", dirent->type == VFS_NODE_TYPE_DIR ? "dir" : "file", name, attr.size);
    }


    vfs_node_t* vfs_node;
    res = vfs_lookup(&VFS_MAKE_ABS_PATH("hello.elf"), &vfs_node);
    assertf(res == VFS_RESULT_OK, "Failed to lookup hello.elf %d", res);

    vfs_node_attr_t attr;
    res = vfs_node->ops->attr(vfs_node, &attr);
    assertf(res == VFS_RESULT_OK, "Failed to get file attr %d", res);

    uint8_t* elf_file = heap_alloc(attr.size);
    res = vfs_node->ops->read(vfs_node, elf_file, attr.size, 0, nullptr);
    assertf(res == VFS_RESULT_OK, "Failed to read hello.elf %d", res);

    // @todo: this is horrifc
    vm_allocator_t* allocator = heap_alloc(sizeof(vm_allocator_t));
    vmm_user_init(allocator, 0x10000, 0x00007fffffffffff);
    process_t* process = process_create(allocator);
    elf64_elf_loader_info_t* loader_info = elf_load(process, (const elf64_elf_header_t*) elf_file);
    assert(loader_info && "Failed to load test module");
    virt_addr_t user_stack_top = vmm_try_alloc_backed(process->allocator, 0x00007ffffffff000 - (16 * PAGE_SIZE_DEFAULT), 16, VM_ACCESS_USER, VM_CACHE_NORMAL, VM_READ_WRITE, true) + (16 * PAGE_SIZE_DEFAULT);
    user_stack_top = sysv_user_stack_init(process, user_stack_top, loader_info);
    thread_t* thread = sched_arch_create_thread_user(process, user_stack_top, loader_info->entry_point);

    process_add_thread(process, thread);
    sched_thread_schedule(thread);

    sched_arch_handoff();

    while(1) { arch_wait_for_interrupt(); }
}

void arch_init_ap(struct limine_mp_info* info) {
    (void) info;
    vm_paging_ap_init(&kernel_allocator);
    vm_address_space_switch(&kernel_allocator);

    init_cpu_local_ap(info->extra_argument);

    setup_gdt();
    dpc_init_queue();
    interrupts_setup_ap();
    lapic_init_ap();
    ipi_init_ap();
    fpu_init_ap();
    userspace_init();

    atomic_store(&arch_ap_finished, 1);
    printf("core %u started\n", info->extra_argument);

    sched_init_ap();
    sched_arch_handoff();

    while(1);
}
