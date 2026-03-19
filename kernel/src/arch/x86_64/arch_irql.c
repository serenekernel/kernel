#include <arch/cpu_local.h>
#include <arch/internal/cr.h>
#include <common/irql.h>

irql_t _arch_irql_set(irql_t new_irql) {
    irql_t old_irql = CPU_LOCAL_READ(current_irql);
    CPU_LOCAL_WRITE(current_irql, new_irql);
    __write_cr8(new_irql);
    return old_irql;
}

irql_t _arch_irql_get() {
    return CPU_LOCAL_READ(current_irql);
}
