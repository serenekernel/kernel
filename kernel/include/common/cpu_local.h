#include <arch/cpu_local.h>

void init_cpu_local_bsp();

void init_cpu_locals(uint32_t core_count);

void init_cpu_local_ap(uint32_t core_id);

uint32_t cpu_local_get_core_lapic_id(uint32_t core_id);
