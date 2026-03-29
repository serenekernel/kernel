#include <arch/hardware/lapic.h>
#include <arch/interrupts.h>
#include <assert.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/interrupts.h>
#include <common/ipi.h>
#include <common/irql.h>
#include <common/vector_alloc.h>
#include <memory/ptm.h>
#include <memory/vm.h>
#include <stdatomic.h>

#include "memory/memory.h"

ipi_t* g_ipi_structs;
uint8_t g_ipi_vector;

void ipi_handler(interrupt_frame_t* frame) {
    (void) frame;

    ipi_t* ipi = &g_ipi_structs[arch_get_core_id()];
    assert(ipi->in_use && "IPI not in use");
    // nl_printf("info | handling ipi %u for core %u\n", ipi->type, arch_get_core_id());
    if(ipi->type == IPI_TYPE_TLB_FLUSH) { ptm_flush_page(ipi->data.tlb_flush.addr, ipi->data.tlb_flush.length); }
    if(ipi->type == IPI_TYPE_DIE) {
        ipi->ready = 0;
        atomic_exchange_explicit(&ipi->in_use, 0, memory_order_release);
        arch_die();
    }
    atomic_exchange_explicit(&ipi->in_use, 0, memory_order_release);
}

void ipi_init_bsp(void) {
    g_ipi_structs = (ipi_t*) vm_map_anon(g_global_address_space, VM_NO_HINT, ALIGN_UP(sizeof(ipi_t) * arch_get_core_count(), PAGE_SIZE_DEFAULT), VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_ZERO);
    g_ipi_vector = alloc_interrupt_vector(3);

    g_ipi_structs[arch_get_core_id()].in_use = 0;
    g_ipi_structs[arch_get_core_id()].ready = 1;
    interrupts_register_handler(g_ipi_vector, ipi_handler);
}

void ipi_init_ap(void) {
    g_ipi_structs[arch_get_core_id()].in_use = 0;
    g_ipi_structs[arch_get_core_id()].ready = 1;
}

void ipi_broadcast_flush_tlb(virt_addr_t addr, size_t length) {
    if(g_ipi_structs == nullptr) return;
    for(uint32_t i = 0; i < arch_get_core_count(); i++) {
        if(i == arch_get_core_id()) continue;

        ipi_t* ipi = &g_ipi_structs[i];
        if(!ipi->ready) continue;
        while(atomic_exchange_explicit(&ipi->in_use, 1, memory_order_acquire) == 1) { arch_pause(); }

        ipi->type = IPI_TYPE_TLB_FLUSH;
        ipi->data.tlb_flush.addr = addr;
        ipi->data.tlb_flush.length = length;
        arch_memory_barrier();
        lapic_send_ipi(cpu_local_get_core_lapic_id(i), g_ipi_vector);
    }
}

void ipi_broadcast_die() {
    if(g_ipi_structs == nullptr) return;
    for(uint32_t i = 0; i < arch_get_core_count(); i++) {
        if(i == arch_get_core_id()) continue;

        ipi_t* ipi = &g_ipi_structs[i];
        if(!ipi->ready) continue;
        while(atomic_exchange_explicit(&ipi->in_use, 1, memory_order_acquire)) { arch_pause(); }

        ipi->type = IPI_TYPE_DIE;
        arch_memory_barrier();
        lapic_send_ipi(cpu_local_get_core_lapic_id(i), g_ipi_vector);
    }
}
