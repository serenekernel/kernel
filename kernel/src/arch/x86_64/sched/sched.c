#include <arch/cpu_local.h>
#include <arch/hardware/fpu.h>
#include <arch/hardware/lapic.h>
#include <arch/interrupts.h>
#include <arch/msr.h>
#include <arch/sched/thread.h>
#include <common/interrupts.h>
#include <common/irql.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <linked_list.h>
#include <memory/heap.h>
#include <memory/memory.h>
#include <memory/vmm.h>
#include <string.h>

typedef struct {
    uint64_t r12, r13, r14, r15, rbp, rbx;
    virt_addr_t thread_init;
    virt_addr_t entry;
    virt_addr_t thread_exit;

    uint64_t rbp_0;
    uint64_t rip_0;
} __attribute__((packed)) init_stack_kernel_t;

typedef struct {
    uint64_t r12, r13, r14, r15, rbp, rbx;
    virt_addr_t thread_init;
    virt_addr_t thread_init_user;
    virt_addr_t entry;
    virt_addr_t user_stack;
} __attribute__((packed)) init_stack_user_t;

extern x86_64_thread_t* __context_switch(x86_64_thread_t* current, x86_64_thread_t* next);
extern void __userspace_init_sysexit();
extern void __userspace_init_fred();

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

x86_64_thread_t* sched_arch_create_thread_common(size_t tid, process_t* process, scheduler_t* sched, virt_addr_t kernel_stack_top, virt_addr_t stack) {
    x86_64_thread_t* thread = heap_alloc(sizeof(x86_64_thread_t));
    thread->common.tid = tid;
    thread->common.state = THREAD_STATE_READY;
    thread->common.sched = sched;
    thread->stack = stack;
    thread->kernel_stack_top = kernel_stack_top;
    thread->common.process = process;
    thread->fpu_area = fpu_alloc_area();
    thread->fsbase = 0;
    thread->gsbase = 0;
    return thread;
}

uint32_t g_next_thread_id = 1;

thread_t* sched_arch_create_thread_kernel(virt_addr_t entry) {
    virt_addr_t kernel_stack_top = vmm_alloc_backed(&kernel_allocator, 16, VM_ACCESS_KERNEL, VM_CACHE_NORMAL, VM_READ_WRITE, true) + (16 * PAGE_SIZE_DEFAULT);

    init_stack_kernel_t* init_stack = (init_stack_kernel_t*) (kernel_stack_top - sizeof(init_stack_kernel_t));
    init_stack->entry = entry;
    init_stack->thread_init = (virt_addr_t) __thread_init_common;
    init_stack->thread_exit = (virt_addr_t) __thread_exit_kernel;

    return &sched_arch_create_thread_common(g_next_thread_id++, nullptr, &CPU_LOCAL_READ(self)->sched, kernel_stack_top, (uintptr_t) init_stack)->common;
}

thread_t* sched_arch_create_thread_user(process_t* process, virt_addr_t entry) {
    virt_addr_t kernel_stack_top = vmm_alloc_backed(&kernel_allocator, 16, VM_ACCESS_KERNEL, VM_CACHE_NORMAL, VM_READ_WRITE, true) + (16 * PAGE_SIZE_DEFAULT);
    virt_addr_t user_stack_top = vmm_alloc_backed(process->allocator, 16, VM_ACCESS_USER, VM_CACHE_NORMAL, VM_READ_WRITE, true) + (16 * PAGE_SIZE_DEFAULT);

    init_stack_user_t* init_stack = (init_stack_user_t*) (kernel_stack_top - sizeof(init_stack_user_t));
    init_stack->entry = entry;
    init_stack->thread_init = (virt_addr_t) __thread_init_common;
    if(x86_64_fred_enabled()) {
        init_stack->thread_init_user = (virt_addr_t) __userspace_init_fred;
    } else {
        init_stack->thread_init_user = (virt_addr_t) __userspace_init_sysexit;
    }
    init_stack->entry = entry;
    init_stack->user_stack = user_stack_top;

    return &sched_arch_create_thread_common(g_next_thread_id++, process, &CPU_LOCAL_READ(self)->sched, kernel_stack_top, (uintptr_t) init_stack)->common;
}

void sched_arch_switch(thread_t* t_current, thread_t* t_next) {
    printf("sched_arch_switch: current=%u, next=%u\n", t_current->tid, t_next->tid);
    x86_64_thread_t* current = CONTAINER_OF(t_current, x86_64_thread_t, common);
    x86_64_thread_t* next = CONTAINER_OF(t_next, x86_64_thread_t, common);

    CPU_LOCAL_WRITE(current_thread, next);
    x86_64_set_rsp0_stack(next->stack);
    if(current->common.process) { fpu_save(current->fpu_area); }
    if(next->common.process) { fpu_load(next->fpu_area); }
    if(current->common.process && next->common.process && current->common.process != next->common.process) {
        vm_address_space_switch(next->common.process->allocator);
    } else if(next->common.process) {
        vm_address_space_switch(next->common.process->allocator);
    }

    write_msr(IA32_KERNEL_GS_BASE_MSR, next->gsbase);
    write_msr(IA32_FS_BASE_MSR, next->fsbase);

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
