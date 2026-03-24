#pragma once
#include <common/sched/thread.h>
#include <linked_list.h>
#include <spinlock.h>

typedef struct scheduler scheduler_t;

struct scheduler {
    spinlock_t lock;
    list_t thread_queue;

    thread_t* idle_thread;
};

thread_t* sched_arch_create_thread_kernel(virt_addr_t entry);
thread_t* sched_arch_create_thread_user(process_t* process, virt_addr_t user_stack_top, virt_addr_t entry);
[[noreturn]] void sched_arch_handoff();
void sched_init_bsp();
void sched_init_ap();
void sched_yield(thread_state_t new_state);
void sched_thread_schedule(thread_t* thread);
thread_t* sched_arch_create_thread();
