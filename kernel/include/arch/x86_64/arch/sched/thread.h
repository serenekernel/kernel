#pragma once

#include <common/sched/thread.h>
#include <stddef.h>

typedef struct {
    uintptr_t stack;
    uintptr_t syscall_stack;
    uintptr_t kernel_stack_top;

    thread_t common;
} x86_64_thread_t;
