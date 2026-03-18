#include <arch/msr.h>
#include <common/cpu_local.h>
#include <memory/vmm.h>

static volatile kernel_cpu_local_t bsp_cpu_local = { 0 };

kernel_cpu_local_t** cpu_local_storage;
uint32_t cpu_local_count;

void init_cpu_local_bsp() {
    __wrmsr(IA32_GS_BASE_MSR, (uint64_t) &bsp_cpu_local);
}

void init_cpu_locals(uint32_t core_count) {
    cpu_local_count = core_count;
    cpu_local_storage = (kernel_cpu_local_t**) vmm_alloc_object(&kernel_allocator, sizeof(kernel_cpu_local_t*) * cpu_local_count);
    for(uint32_t i = 0; i < cpu_local_count; i++) { cpu_local_storage[i] = (kernel_cpu_local_t*) vmm_alloc_object(&kernel_allocator, sizeof(kernel_cpu_local_t)); }
}

void init_cpu_local_ap(uint32_t core_id) {
    kernel_cpu_local_t* cpu_local = cpu_local_storage[core_id];
    __wrmsr(IA32_GS_BASE_MSR, (uint64_t) cpu_local);
}
