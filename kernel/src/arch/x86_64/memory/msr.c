#include <arch/msr.h>
#include <assert.h>
#include <common/irql.h>
#include <common/requests.h>
#include <limine.h>

#include "arch/cpu_local.h"
#include "memory/memory.h"

// @note: these just live here for now
[[nodiscard]] uint64_t read_msr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t) hi << 32) | lo;
}

void write_msr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t) (value & 0xFFFFFFFF);
    uint32_t hi = (uint32_t) (value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

extern uint32_t __probe_msr(uint32_t msr);
extern uint32_t __probe_msr_handler();

bool probe_msr(uint32_t msr) {
    (void) msr;
    irql_t old_irql = irql_raise(IRQL_HIGH);

    assert(CPU_LOCAL_READ(faultable.enabled) == false);
    CPU_LOCAL_WRITE(faultable.redirect, (virt_addr_t) __probe_msr_handler);
    CPU_LOCAL_WRITE(faultable.enabled, true);

    uint32_t ret = __probe_msr(msr);
    irql_lower(old_irql);

    printf("probe_msr: ret=%u\n", ret);
    return ret;
}
