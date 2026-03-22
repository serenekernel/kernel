#include <arch/cpu_local.h>
#include <arch/hardware/lapic.h>
#include <arch/msr.h>
#include <common/cpu_local.h>
#include <memory/vmm.h>
#include <string.h>

#include <common/irql.h>
#include <linked_list.h>

static volatile kernel_cpu_local_t bsp_cpu_local = { 0 };

kernel_cpu_local_t* cpu_local_storage;
uint32_t cpu_local_count;

void init_cpu_local_data(kernel_cpu_local_t* cpu_local, uint32_t core_id, uint32_t lapic_id) {
    cpu_local->core_id = core_id;
    cpu_local->lapic_id = lapic_id;
    cpu_local->current_irql = IRQL_PASSIVE;
    cpu_local->dpc_queue = NULL;
    cpu_local->preempt_pending = false;
    cpu_local->current_thread = NULL;
    cpu_local->self = cpu_local;
}

void init_cpu_local_bsp() {
    init_cpu_local_data((kernel_cpu_local_t*) &bsp_cpu_local, 0, 0);
    write_msr(IA32_GS_BASE_MSR, (uint64_t) &bsp_cpu_local);
}

void init_cpu_locals(uint32_t core_count) {
    cpu_local_count = core_count;
    cpu_local_storage = (kernel_cpu_local_t*) vmm_alloc_bytes(&kernel_allocator, sizeof(kernel_cpu_local_t) * cpu_local_count);
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
