#pragma once
#include <common/dpc.h>
#include <common/sched/thread.h>
#include <stddef.h>

typedef struct {
    uintptr_t stack;
    uintptr_t syscall_stack;
    uintptr_t kernel_stack_top;

    thread_t common;

    struct {
        bool in_flight;
        uintptr_t address;
        dpc_t* dpc;
    } vm_fault;

    void* fpu_area;
    uintptr_t fsbase;
    uintptr_t gsbase;
} x86_64_thread_t;
