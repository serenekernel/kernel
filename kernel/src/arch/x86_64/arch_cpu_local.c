#include <arch/cpu_local.h>
#include <arch/hardware/lapic.h>
#include <arch/msr.h>
#include <common/cpu_local.h>
#include <common/irql.h>
#include <linked_list.h>
#include <memory/vm.h>
#include <string.h>

#include "memory/memory.h"

static volatile kernel_cpu_local_t bsp_cpu_local = { 0 };

kernel_cpu_local_t* cpu_local_storage;
uint32_t cpu_local_count;

void init_cpu_local_data(kernel_cpu_local_t* cpu_local, uint32_t core_id, uint32_t lapic_id) {
    cpu_local->core_id = core_id;
    cpu_local->lapic_id = lapic_id;
    cpu_local->current_irql = IRQL_PASSIVE;
    cpu_local->dpc_queue = NULL;
    cpu_local->dpc_executing = false;
    cpu_local->preempt_pending = false;
    cpu_local->current_thread = NULL;
    cpu_local->sched = (scheduler_t) {
        .lock = SPINLOCK_INIT,
        .thread_queue = LIST_INIT,
        .idle_thread = NULL,
    };
    cpu_local->self = cpu_local;
}

void init_cpu_local_bsp() {
    init_cpu_local_data((kernel_cpu_local_t*) &bsp_cpu_local, 0, 0);
    write_msr(IA32_GS_BASE_MSR, (uint64_t) &bsp_cpu_local);
}

void init_cpu_locals(uint32_t core_count) {
    cpu_local_count = core_count;
    cpu_local_storage = (kernel_cpu_local_t*) vm_map_anon(g_global_address_space, VM_NO_HINT, ALIGN_UP(sizeof(kernel_cpu_local_t) * cpu_local_count, PAGE_SIZE_DEFAULT), VM_PROT_RW, VM_CACHE_NORMAL, true);
    bsp_cpu_local.lapic_id = lapic_get_id();
    memcpy(cpu_local_storage, (void*) &bsp_cpu_local, sizeof(kernel_cpu_local_t));
    write_msr(IA32_GS_BASE_MSR, (uint64_t) &cpu_local_storage[0]);
}

void init_cpu_local_ap(uint32_t core_id) {
    init_cpu_local_data(&cpu_local_storage[core_id], core_id, lapic_get_id());
    write_msr(IA32_GS_BASE_MSR, (uint64_t) &cpu_local_storage[core_id]);
}

uint32_t cpu_local_get_core_lapic_id(uint32_t core_id) {
    return cpu_local_storage[core_id].lapic_id;
}
