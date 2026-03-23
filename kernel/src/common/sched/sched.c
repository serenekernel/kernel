#include <arch/cpu_local.h>
#include <arch/hardware/lapic.h>
#include <arch/sched/thread.h>
#include <assert.h>
#include <common/arch.h>
#include <common/interrupts.h>
#include <common/irql.h>
#include <common/sched/sched.h>
#include <linked_list.h>
#include <memory/heap.h>
#include <memory/memory.h>
#include <memory/vmm.h>
#include <spinlock.h>

#define LAPIC_TIMER_VECTOR 0x20

void sched_arch_switch(thread_t* t_current, thread_t* t_next);

void idle_thread_entry() {
    while(1) { asm("hlt"); }
}

thread_t* sched_next_thread(scheduler_t* sched) {
    spinlock_lock(&sched->lock);
    list_node_t* node = list_pop_front(&sched->thread_queue);
    thread_t* next = node ? CONTAINER_OF(node, thread_t, list_node_sched) : nullptr;
    spinlock_unlock(&sched->lock);
    return next;
}

static void sched_switch_next(thread_state_t new_state) {
    scheduler_t* sched = &CPU_LOCAL_READ(self)->sched;
    thread_t* current = &CPU_LOCAL_READ(current_thread)->common;
    thread_t* next = sched_next_thread(sched);

    if(next == nullptr && current != current->sched->idle_thread) next = current->sched->idle_thread;
    if(next == nullptr || next == current) {
        lapic_timer_oneshot_ms(10);
        return;
    }

    current->state = new_state;
    current->sched = sched;
    sched_arch_switch(current, next);

    lapic_timer_oneshot_ms(10);
}

static void sched_timer_handler(interrupt_frame_t* frame) {
    (void) frame;
    CPU_LOCAL_WRITE(preempt_pending, true);
}

void sched_init_bsp() {
    scheduler_t* sched = &CPU_LOCAL_READ(self)->sched;
    sched->thread_queue = LIST_INIT;
    sched->lock = SPINLOCK_INIT;

    sched->idle_thread = sched_arch_create_thread_kernel((virt_addr_t) idle_thread_entry);

    interrupts_register_handler(LAPIC_TIMER_VECTOR, sched_timer_handler);
}

void sched_init_ap() {
    scheduler_t* sched = &CPU_LOCAL_READ(self)->sched;
    sched->thread_queue = LIST_INIT;
    sched->lock = SPINLOCK_INIT;

    sched->idle_thread = sched_arch_create_thread_kernel((virt_addr_t) idle_thread_entry);
}

void sched_yield(thread_state_t new_state) {
    irql_t irql = irql_raise(IRQL_DISPATCH);
    assert(irql == IRQL_PASSIVE && "sched_yield called with IRQL != IRQL_PASSIVE");

    sched_switch_next(new_state);

    irql_lower(irql);
    return;
}

void sched_thread_schedule(thread_t* thread) {
    thread->state = THREAD_STATE_READY;
    spinlock_lock(&thread->sched->lock);
    list_push_back(&thread->sched->thread_queue, &thread->list_node_sched);
    spinlock_unlock(&thread->sched->lock);
}
