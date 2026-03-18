#include <common/arch.h>
#include <common/spinlock.h>

static inline bool spinlock_try_lock(volatile spinlock_t* lock) {
    return !__atomic_test_and_set(&lock->__lock, __ATOMIC_ACQUIRE);
}

static inline void spinlock_unlock_raw(volatile spinlock_t* lock) {
    __atomic_clear(&lock->__lock, __ATOMIC_RELEASE);
}

static inline void spinlock_lock_raw(volatile spinlock_t* lock) {
    while(true) {
        if(spinlock_try_lock(lock)) return;

        while(__atomic_load_n(&lock->__lock, __ATOMIC_RELAXED)) { arch_pause(); }
    }
}


irql_t spinlock_lock(spinlock_t* lock) {
    irql_t prev = irql_raise(IRQL_DISPATCH);
    spinlock_lock_raw(lock);
    return prev;
}

irql_t spinlock_lock_raise(spinlock_t* lock, irql_t irql) {
    irql_t prev = irql_raise(irql);
    spinlock_lock_raw(lock);
    return prev;
}

void spinlock_unlock(spinlock_t* lock, irql_t irql) {
    spinlock_unlock_raw(lock);
    irql_lower(irql);
}
