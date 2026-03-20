#include <assert.h>
#include <common/irql.h>
#include <common/vector_alloc.h>
#include <lib/spinlock.h>
#include <stdbool.h>
#include <stdint.h>

static uint8_t g_vector_bitmap[256] = { 0 };
static spinlock_t g_vector_lock = SPINLOCK_INIT;

#define VECTOR_START 0x20
#define VECTOR_END 0xEF

uint8_t alloc_interrupt_vector(irql_t target_irql) {
    spinlock_lock(&g_vector_lock);
    assert(target_irql > 1 && target_irql <= 14 && "Invalid IRQL for interrupt vector allocation");

    for(int v = 0; v <= 0xf; ++v) {
        uint8_t vector = (target_irql << 4) | v;
        if(!g_vector_bitmap[vector]) {
            g_vector_bitmap[vector] = 1;
            spinlock_unlock(&g_vector_lock);
            return vector;
        }
    }
    spinlock_unlock(&g_vector_lock);
    return -1;
}

uint8_t alloc_specific_interrupt_vector(uint8_t vector) {
    if(vector < VECTOR_START || vector > VECTOR_END) { return -1; }
    spinlock_lock(&g_vector_lock);
    if(g_vector_bitmap[vector]) {
        spinlock_unlock(&g_vector_lock);
        return -1;
    }
    g_vector_bitmap[vector] = 1;
    spinlock_unlock(&g_vector_lock);
    return 0;
}

void free_interrupt_vector(uint8_t vector) {
    spinlock_lock(&g_vector_lock);
    g_vector_bitmap[vector] = 0;
    spinlock_unlock(&g_vector_lock);
}
