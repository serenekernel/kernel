#pragma once
#include <common/arch.h>
#include <common/interrupts.h>
#include <common/irql.h>
#include <stdint.h>

typedef struct {
    irql_t __locked_irql;
    uint32_t __lock;
} spinlock_t;

#define SPINLOCK_INIT ((spinlock_t) { 0, 0 })

// @returns: the previous IRQL
void spinlock_lock(spinlock_t* lock);
void spinlock_lock_raise(spinlock_t* lock, irql_t irql);
void spinlock_unlock(spinlock_t* lock);
