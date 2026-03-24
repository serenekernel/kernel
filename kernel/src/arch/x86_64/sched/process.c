#include <common/sched/process.h>
#include <common/sched/thread.h>
#include <linked_list.h>
#include <memory/heap.h>
#include <spinlock.h>
#include <stdatomic.h>

uint64_t process_next_pid = 0;

process_t* process_create(vm_allocator_t* allocator) {
    process_t* process = (process_t*) heap_alloc(sizeof(process_t));
    process->allocator = allocator;
    process->thread_list = LIST_INIT;
    process->thread_lock = SPINLOCK_INIT;
    process->pid = atomic_fetch_add(&process_next_pid, 1);
    return process;
}

process_t* process_add_thread(process_t* process, thread_t* thread) {
    spinlock_lock(&process->thread_lock);
    list_push_back(&process->thread_list, &thread->list_node_proc);
    spinlock_unlock(&process->thread_lock);
    return process;
}
