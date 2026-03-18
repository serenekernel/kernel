#include "arch/hardware/lapic.h"

#include <arch/internal/cpuid.h>
#include <arch/internal/cr.h>
#include <arch/internal/gdt.h>
#include <assert.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/interrupts.h>
#include <common/requests.h>
#include <memory/memory.h>
#include <memory/pmm.h>
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

    setup_protections();
}

void setup_acpi() {
    virt_addr_t temp_buffer = vmm_alloc_object(&kernel_allocator, 4096);
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

void arch_init_bsp() {
    init_cpu_local_bsp();

    setup_memory();
    setup_arch();

    printf("Hello, %s!\n", arch_get_name());
    for(size_t i = 0; i < mp_request.response->cpu_count; i++) { printf("CPU %zu: lapic_id: %u processor_id %u\n", i, mp_request.response->cpus[i]->lapic_id, mp_request.response->cpus[i]->processor_id); }

    __atomic_store_n(&arch_ap_finished, 0, __ATOMIC_RELAXED);
    while(1) { __asm__("hlt"); }
    //     enable_interrupts();
    //     for(size_t i = 0; i < mp_request.response->cpu_count; i++) {
    //         if(mp_request.response->cpus[i]->lapic_id == lapic_get_id()) { continue; }

    //         printf("Starting AP with lapic id %u\n", mp_request.response->cpus[i]->lapic_id);
    //         kernel_cpu_local_t* ap_cpu_local = (kernel_cpu_local_t*) vmm_alloc_object(&kernel_allocator, sizeof(kernel_cpu_local_t));
    //         assert(ap_cpu_local != NULL && "Failed to allocate cpu local for ap");
    //         mp_request.response->cpus[i]->extra_argument = (uint64_t) ap_cpu_local;
    //         atomic_store(&mp_request.response->cpus[i]->goto_address, &arch_init_ap);
    //         while(__atomic_load_n(&arch_ap_finished, __ATOMIC_RELAXED) == 0) { arch_pause(); }
    //     }
    // }
}
void arch_init_ap(struct limine_mp_info* info) {
    (void) info;
    while(1);
}
