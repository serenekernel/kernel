#include <arch/hardware/lapic.h>
#include <arch/internal/cpuid.h>
#include <arch/internal/cr.h>
#include <arch/internal/gdt.h>
#include <arch/interrupts.h>
#include <arch/io.h>
#include <assert.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/dpc.h>
#include <common/interrupts.h>
#include <common/io.h>
#include <common/ipi.h>
#include <common/irql.h>
#include <common/requests.h>
#include <memory/heap.h>
#include <memory/memory.h>
#include <memory/pmm.h>
#include <memory/slab.h>
#include <memory/vmm.h>
#include <stdatomic.h>
#include <stdio.h>
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

    uint32_t current_core_id = 1;
    for(size_t i = 0; i < mp_request.response->cpu_count; i++) {
        printf("CPU %zu: lapic_id: %u processor_id %u\n", i, mp_request.response->cpus[i]->lapic_id, mp_request.response->cpus[i]->processor_id);
        if(mp_request.response->cpus[i]->lapic_id == lapic_get_id()) { continue; }
        mp_request.response->cpus[i]->extra_argument = current_core_id++;
    }

    init_aps();

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

    printf("core %u started\n", info->extra_argument);

    while(1) { arch_wait_for_interrupt(); }
}
