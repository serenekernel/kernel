#include <assert.h>
#include <common/cpu_local.h>
#include <common/dpc.h>
#include <common/irql.h>
#include <lib/spinlock.h>
#include <linked_list.h>
#include <memory/heap.h>
#include <memory/memory.h>
#include <memory/vmm.h>

void dpc_init_queue() {
    dpc_queue_t* queue = (dpc_queue_t*) heap_alloc(sizeof(dpc_queue_t));
    assert(queue != NULL);

    queue->dpc_list = LIST_INIT;
    queue->lock = SPINLOCK_INIT;
    CPU_LOCAL_WRITE(dpc_queue, queue);
}

dpc_t* dpc_create(fn_dpc_handler_t handler, bool one_shot) {
    dpc_t* dpc = (dpc_t*) heap_alloc(sizeof(dpc_t));
    assert(dpc != NULL);

    dpc->handler = handler;
    dpc->in_use = false;
    dpc->one_shot = one_shot;

    return dpc;
}

void dpc_destroy(dpc_t* dpc) {
    assert(dpc != NULL);
    heap_free(dpc, sizeof(dpc_t));
}

void dpc_enqueue(dpc_t* dpc, void* arg) {
    dpc->arg = arg;
    dpc->in_use = true;
    dpc_queue_t* queue = CPU_LOCAL_READ(dpc_queue);
    assert(queue != NULL);

    spinlock_lock_raise(&queue->lock, IRQL_HIGH);
    list_push_back(&queue->dpc_list, &dpc->list_node);
    spinlock_unlock(&queue->lock);
}

bool dpc_queue_empty() {
    dpc_queue_t* queue = CPU_LOCAL_READ(dpc_queue);
    if(queue == NULL) return true;
    return queue->dpc_list.count == 0;
}

void dpc_execute_front(dpc_queue_t* queue) {
    assert(irql_get() == IRQL_DISPATCH);

    spinlock_lock_raise(&queue->lock, IRQL_HIGH);
    list_node_t* list_node = list_pop_front(&queue->dpc_list);
    dpc_t* dpc = CONTAINER_OF(list_node, dpc_t, list_node);
    dpc->in_use = true;
    spinlock_unlock(&queue->lock);

    dpc->handler(dpc, dpc->arg);
    dpc->in_use = false;
    if(dpc->one_shot) { dpc_destroy(dpc); }
}

void dpc_execute_all() {
    assert(irql_get() == IRQL_DISPATCH);
    dpc_queue_t* queue = CPU_LOCAL_READ(dpc_queue);
    assert(queue != NULL);

    while(true) {
        spinlock_lock_raise(&queue->lock, IRQL_HIGH);
        uint64_t dpc_count = queue->dpc_list.count;
        spinlock_unlock(&queue->lock);

        if(dpc_count == 0) break;

        dpc_execute_front(queue);
    }
}
