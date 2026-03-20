#include <arch/msr.h>
#include <common/cpu_local.h>
#include <memory/vmm.h>
#include <string.h>

static volatile kernel_cpu_local_t bsp_cpu_local = { 0 };

kernel_cpu_local_t* cpu_local_storage;
uint32_t cpu_local_count;

void init_cpu_local_bsp() {
    __wrmsr(IA32_GS_BASE_MSR, (uint64_t) &bsp_cpu_local);
}

void init_cpu_locals(uint32_t core_count) {
    cpu_local_count = core_count;
    cpu_local_storage = (kernel_cpu_local_t*) vmm_alloc_bytes(&kernel_allocator, sizeof(kernel_cpu_local_t) * cpu_local_count);
    memcpy(cpu_local_storage, (void*) &bsp_cpu_local, sizeof(kernel_cpu_local_t));
    __wrmsr(IA32_GS_BASE_MSR, (uint64_t) &cpu_local_storage[0]);
}

void init_cpu_local_ap(uint32_t core_id) {
    __wrmsr(IA32_GS_BASE_MSR, (uint64_t) &cpu_local_storage[core_id]);
    cpu_local_storage[core_id].core_id = core_id;
}

uint32_t cpu_local_get_core_lapic_id(uint32_t core_id) {
    return cpu_local_storage[core_id].lapic_id;
}
