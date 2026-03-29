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
#include <common/tty.h>
#include <common/userspace/userspace.h>
#include <memory/heap.h>
#include <memory/memory.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <memory/slab.h>
#include <memory/vm.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <uacpi/uacpi.h>

#include "common/fs/stdiofs.h"
#include "device/ps2kbd.h"

void setup_protections() {
    arch_memory_barrier();
    uint64_t cr4 = __read_cr4();
    if(__cpuid_is_feature_supported(CPUID_FEATURE_UMIP)) {
        cr4 |= (1 << 11); // cr4.UMIP
        LOG_OKAY("UMIP: supported\n");
    } else {
        LOG_WARN("UMIP: not supported\n");
    }

    if(__cpuid_is_feature_supported(CPUID_FEATURE_SMEP)) {
        cr4 |= (1 << 20); // cr4.SMEP
        LOG_OKAY("SMEP: supported\n");
    } else {
        LOG_WARN("SMEP: not supported\n");
    }

    if(__cpuid_is_feature_supported(CPUID_FEATURE_SMAP)) {
        LOG_OKAY("SMAP: supported\n");
        cr4 |= (1 << 21); // cr4.SMAP
    } else {
        LOG_WARN("SMAP: not supported\n");
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

static vm_region_t g_hhdm_region;

void setup_memory() {
    pmm_init();
    ptm_init_kernel_bsp();

    size_t hhdm_size = 0;
    for(size_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap_request.response->entries[i];
        if(entry->base + entry->length > hhdm_size) { hhdm_size = entry->base + entry->length; }
    }

    g_hhdm_region.address_space = g_global_address_space;
    g_hhdm_region.base = hhdm_request.response->offset;
    g_hhdm_region.length = hhdm_size;
    g_hhdm_region.protection = VM_PROT_RW;
    g_hhdm_region.cache = VM_CACHE_NORMAL;
    g_hhdm_region.dynamically_backed = false;
    g_hhdm_region.type = VM_REGION_TYPE_DIRECT;
    g_hhdm_region.type_data.direct.physical_address = 0;
    rb_insert(&g_global_address_space->regions_tree, &g_hhdm_region.region_tree_node);

    slab_cache_init();
    init_heap();


    setup_protections();
}

void setup_acpi() {
    void* temp_buffer = vm_map_anon(g_global_address_space, VM_NO_HINT, 4096, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_ZERO);
    uacpi_status ret = uacpi_setup_early_table_access(temp_buffer, 4096);
    assertf(!uacpi_unlikely_error(ret), "uacpi_setup_early_table_access error: %s", uacpi_status_to_string(ret));
}

void setup_arch() {
    LOG_INFO("CPU Vendor: %s\n", __cpuid_get_vendor_string());
    LOG_INFO("CPU Name: %s\n", __cpuid_get_name_string());

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

        LOG_OKAY("Starting AP with lapic id %u\n", mp_request.response->cpus[i]->lapic_id);
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
    LOG_OKAY("Hello, %s!\n", arch_get_name());

    ipi_init_bsp();
    init_cpu_locals(mp_request.response->cpu_count);
    fpu_init_bsp();

    uint32_t current_core_id = 1;
    for(size_t i = 0; i < mp_request.response->cpu_count; i++) {
        LOG_INFO("CPU %zu: lapic_id: %u processor_id %u\n", i, mp_request.response->cpus[i]->lapic_id, mp_request.response->cpus[i]->processor_id);
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
    if(res != VFS_RESULT_OK) {
        LOG_FAIL("Failed to mount initramfs (%d)\n", res);
        arch_die();
    }
    LOG_OKAY("mounted initramfs\n");

    res = vfs_mount(&fs_stdio_ops, "/dev/console", nullptr);
    if(res != VFS_RESULT_OK) {
        LOG_FAIL("Failed to mount stdiofs (%d)\n", res);
        arch_die();
    }

    vfs_node_t* root_node;
    res = vfs_root(&root_node);
    if(res != VFS_RESULT_OK) {
        LOG_FAIL("Failed to get root node (%d)\n", res);
        arch_die();
    }

    size_t offset = 0;
    while(1) {
        char* dirent_name;
        res = root_node->ops->readdir(root_node, &offset, &dirent_name);
        if(res == VFS_RESULT_ERR_NOT_FOUND) { break; }
        assertf(res == VFS_RESULT_OK, "Failed to readdir %d", res);
        if(dirent_name == nullptr) { break; }

        vfs_node_t* dirent;
        res = root_node->ops->lookup(root_node, dirent_name, &dirent);
        assertf(res == VFS_RESULT_OK, "Failed to lookup dirent %d", res);

        vfs_node_attr_t attr;
        res = dirent->ops->attr(dirent, &attr);
        assertf(res == VFS_RESULT_OK, "Failed to get dirent attr %d", res);
        LOG_INFO("%s: %s %d bytes\n", dirent->type == VFS_NODE_TYPE_DIR ? "dir" : "file", dirent_name, attr.size);
    }

    // @todo: this is horrifc
    vm_address_space_t* process_as = heap_alloc(sizeof(vm_address_space_t));
    ptm_init_user(process_as);
    process_t* process = process_create(process_as);
    elf64_elf_loader_info_t* elf_info;
    bool loaded_elf = elf_load_file(process, &VFS_MAKE_ABS_PATH("/usr/bin/hello"), &elf_info);
    assert(loaded_elf && "Failed to load init file");
    size_t stack_virt_size = 1024 * PAGE_SIZE_DEFAULT;
    virt_addr_t user_stack = (virt_addr_t) vm_map_anon(process_as, (void*) (USERSPACE_END - (10 * PAGE_SIZE_DEFAULT) - stack_virt_size), stack_virt_size, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_FIXED | VM_FLAG_ZERO | VM_FLAG_DYNAMICALLY_BACKED);
    assert(user_stack != 0 && "Failed to allocate user stack");
    LOG_INFO("user_stack: %p\n", user_stack);
    virt_addr_t user_stack_top = user_stack + stack_virt_size;
    uintptr_t user_rsp = sysv_user_stack_init(process, user_stack_top, elf_info);
    LOG_INFO("user_rsp: %p\n", user_rsp);
    assert(user_rsp % 16 == 0 && "user_rsp is not 16-byte aligned");
    thread_t* thread = sched_arch_create_thread_user(process, user_rsp, elf_info->executable_entry_point);

    g_tty = tty_init();
    ps2kbd_init();

    process_add_thread(process, thread);
    sched_thread_schedule(thread);

    LOG_OKAY("Sched Handoff\n");
    sched_arch_handoff();

    while(1) { arch_wait_for_interrupt(); }
}

void arch_init_ap(struct limine_mp_info* info) {
    (void) info;
    ptm_init_kernel_ap();

    init_cpu_local_ap(info->extra_argument);

    setup_gdt();
    dpc_init_queue();
    interrupts_setup_ap();
    lapic_init_ap();
    ipi_init_ap();
    fpu_init_ap();
    userspace_init();

    atomic_store(&arch_ap_finished, 1);
    LOG_OKAY("core %u started\n", info->extra_argument);

    sched_init_ap();
    sched_arch_handoff();

    while(1);
}
