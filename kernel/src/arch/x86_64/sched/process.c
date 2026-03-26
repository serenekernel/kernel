#include <common/sched/process.h>
#include <common/sched/thread.h>
#include <linked_list.h>
#include <memory/heap.h>
#include <spinlock.h>
#include <stdatomic.h>

#include "common/fs/fd_store.h"
#include "common/fs/vfs.h"

uint64_t process_next_pid = 0;

process_t* process_create(vm_allocator_t* allocator) {
    process_t* process = (process_t*) heap_alloc(sizeof(process_t));
    process->allocator = allocator;
    process->thread_list = LIST_INIT;
    process->thread_lock = SPINLOCK_INIT;
    process->pid = atomic_fetch_add(&process_next_pid, 1);
    process->fd_store = fd_store_create();

    vfs_result_t res;
    res = fd_store_open_fixed(process->fd_store, &VFS_MAKE_ABS_PATH("/dev/console/stdin"), 0);
    assert(res == VFS_RESULT_OK);
    res = fd_store_open_fixed(process->fd_store, &VFS_MAKE_ABS_PATH("/dev/console/stdout"), 1);
    assert(res == VFS_RESULT_OK);
    res = fd_store_open_fixed(process->fd_store, &VFS_MAKE_ABS_PATH("/dev/console/stderr"), 2);
    assert(res == VFS_RESULT_OK);

    return process;
}

process_t* process_add_thread(process_t* process, thread_t* thread) {
    spinlock_lock(&process->thread_lock);
    list_push_back(&process->thread_list, &thread->list_node_proc);
    spinlock_unlock(&process->thread_lock);
    return process;
}
