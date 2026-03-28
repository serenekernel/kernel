#include <assert.h>
#include <common/cpu_local.h>
#include <common/dpc.h>
#include <common/irql.h>
#include <common/sched/sched.h>

// @note: these are internal methods
irql_t _arch_irql_set(irql_t new_irql);
irql_t _arch_irql_get();

// Raises the IRQL to the next higher level
// @returns: the previous IRQL level
irql_t irql_raise(irql_t new_level) {
    irql_t old_irql = _arch_irql_get();
    assertf(old_irql <= new_level, "IRQL raised above new level: old=%d new=%d", old_irql, new_level);
    if(old_irql != new_level) { _arch_irql_set(new_level); }
    return old_irql;
}

// Lowers the IRQL to the next lower level
// @returns: the previous IRQL level
irql_t irql_lower(irql_t new_level) {
    irql_t old_irql = _arch_irql_get();
    assertf(old_irql >= new_level, "IRQL lowered below new level: old=%d new=%d", old_irql, new_level);

    if(old_irql == new_level) { return old_irql; }

    if(new_level <= IRQL_DISPATCH && !CPU_LOCAL_READ(dpc_executing) && !dpc_queue_empty()) {
        _arch_irql_set(IRQL_DISPATCH);
        CPU_LOCAL_WRITE(dpc_executing, true);
        dpc_execute_all();
        CPU_LOCAL_WRITE(dpc_executing, false);
    }

    if(new_level == IRQL_PASSIVE && CPU_LOCAL_EXCHANGE(preempt_pending, false)) {
        _arch_irql_set(IRQL_PASSIVE);
        sched_yield(THREAD_STATE_READY);
        return old_irql;
    }

    _arch_irql_set(new_level);
    return old_irql;
}

irql_t irql_get() {
    return _arch_irql_get();
}
