#pragma once
#include <linked_list.h>
#include <memory/vmm.h>
#include <spinlock.h>

typedef struct thread thread_t;
typedef struct process process_t;

struct process {
    uint64_t pid;
    vm_allocator_t* allocator;

    spinlock_t thread_lock;
    list_t thread_list;
};

process_t* process_create(vm_allocator_t* allocator);
process_t* process_add_thread(process_t* process, thread_t* thread);
