#pragma once
#include <common/arch.h>
#include <common/interrupts.h>
#include <common/irql.h>
#include <stdint.h>

typedef volatile struct {
    volatile uint32_t __lock;
} spinlock_t;

#define SPINLOCK_INIT (spinlock_t){ 0 };

// @returns: the previous IRQL
[[nodiscard]] irql_t spinlock_lock(spinlock_t* lock);
[[nodiscard]] irql_t spinlock_lock_raise(spinlock_t* lock, irql_t irql);
void spinlock_unlock(spinlock_t* lock, irql_t irql);
