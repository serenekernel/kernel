#pragma once
#include <common/fs/fd_store.h>
#include <common/fs/vfs.h>
#include <lib/buffer.h>
#include <linked_list.h>
#include <memory/vm.h>
#include <spinlock.h>

typedef struct thread thread_t;
typedef struct process process_t;

extern list_t process_list;

struct process {
    uint64_t pid;
    uint64_t process_group_pid;
    vm_address_space_t* address_space;

    fd_store_t* fd_store;
    vfs_node_t* cwd;

    spinlock_t thread_lock;
    list_t thread_list;
    list_node_t process_list_node;
};

process_t* process_create(vm_address_space_t* allocator);
process_t* process_add_thread(process_t* process, thread_t* thread);
process_t* process_get_by_pid(uint64_t pid);
