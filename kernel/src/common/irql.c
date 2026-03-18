#include "common/dpc.h"

#include <assert.h>
#include <common/irql.h>

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

    if(new_level <= IRQL_DISPATCH && !dpc_queue_empty()) {
        _arch_irql_set(IRQL_DISPATCH);
        dpc_execute_all();
    }

    _arch_irql_set(new_level);
    return old_irql;
}

irql_t irql_get() {
    return _arch_irql_get();
}
