#include <arch/cpu_local.h>
#include <arch/interrupts.h>
#include <arch/sched/thread.h>
#include <common/irql.h>
#include <common/sched/sched.h>
#include <linked_list.h>
#include <memory/heap.h>
#include <string.h>

#include "arch/hardware/lapic.h"
#include "common/interrupts.h"
#include "memory/memory.h"
#include "memory/vmm.h"

typedef struct {
    uint64_t r12, r13, r14, r15, rbp, rbx;
    virt_addr_t thread_init;
    virt_addr_t entry;
    virt_addr_t thread_exit;

    uint64_t rbp_0;
    uint64_t rip_0;
} __attribute__((packed)) init_stack_kernel_t;

extern x86_64_thread_t* __context_switch(x86_64_thread_t* current, x86_64_thread_t* next);

void __sched_thread_drop(thread_t* thread) {
    if(thread == thread->sched->idle_thread) return;

    // @todo: reap
    switch(thread->state) {
        case THREAD_STATE_READY: sched_thread_schedule(thread); break;
        case THREAD_STATE_DEAD:  break;
        default:                 assertf(false, "invalid state on drop %d", thread->state);
    }
}

void __thread_init_common(x86_64_thread_t* prev) {
    __sched_thread_drop(&prev->common);
    lapic_timer_oneshot_ms(10);
    irql_lower(IRQL_PASSIVE);
}

void __thread_exit_kernel() {
    sched_yield(THREAD_STATE_DEAD);
}


x86_64_thread_t* sched_arch_create_thread_common(size_t tid, scheduler_t* sched, virt_addr_t kernel_stack_top, virt_addr_t stack) {
    x86_64_thread_t* thread = heap_alloc(sizeof(x86_64_thread_t));
    thread->common.tid = tid;
    thread->common.state = THREAD_STATE_READY;
    thread->common.sched = sched;
    thread->stack = stack;
    thread->kernel_stack_top = kernel_stack_top;

    return thread;
}

uint32_t g_next_thread_id = 1;

thread_t* sched_arch_create_thread_kernel(virt_addr_t entry) {
    virt_addr_t kernel_stack_top = vmm_alloc_backed(&kernel_allocator, 16, VM_ACCESS_KERNEL, VM_CACHE_NORMAL, VM_READ_WRITE, true) + (16 * PAGE_SIZE_DEFAULT);

    init_stack_kernel_t* init_stack = (init_stack_kernel_t*) (kernel_stack_top - sizeof(init_stack_kernel_t));
    init_stack->entry = entry;
    init_stack->thread_init = (virt_addr_t) __thread_init_common;
    init_stack->thread_exit = (virt_addr_t) __thread_exit_kernel;

    return &sched_arch_create_thread_common(g_next_thread_id++, &CPU_LOCAL_READ(self)->sched, kernel_stack_top, (uintptr_t) init_stack)->common;
}

void sched_arch_switch(thread_t* t_current, thread_t* t_next) {
    x86_64_thread_t* current = CONTAINER_OF(t_current, x86_64_thread_t, common);
    x86_64_thread_t* next = CONTAINER_OF(t_next, x86_64_thread_t, common);

    CPU_LOCAL_WRITE(current_thread, next);
    x86_64_thread_t* prev = __context_switch(current, next);
    __sched_thread_drop(&prev->common);
}


[[noreturn]] void sched_arch_handoff() {
    thread_t* bsp_thread = sched_arch_create_thread_kernel(0);
    bsp_thread->state = THREAD_STATE_DEAD;

    scheduler_t* sched = &CPU_LOCAL_READ(self)->sched;
    thread_t* idle_thread = sched->idle_thread;
    interrupts_enable();
    sched_arch_switch(bsp_thread, idle_thread);
    while(1);
}
