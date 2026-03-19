#include <assert.h>
#include <common/cpu_local.h>
#include <common/dpc.h>
#include <common/irql.h>
#include <common/spinlock.h>
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

dpc_t* dpc_create(fn_dpc_handler_t handler) {
    dpc_t* dpc = (dpc_t*) heap_alloc(sizeof(dpc_t));
    assert(dpc != NULL);

    dpc->handler = handler;

    return dpc;
}

void dpc_destroy(dpc_t* dpc) {
    assert(dpc != NULL);
    heap_free(dpc, sizeof(dpc_t));
}

void dpc_enqueue(dpc_t* dpc, void* arg) {
    dpc->arg = arg;

    dpc_queue_t* queue = CPU_LOCAL_READ(dpc_queue);
    assert(queue != NULL);

    spinlock_lock(&queue->lock);
    list_push_back(&queue->dpc_list, &dpc->list_node);
    spinlock_unlock(&queue->lock);
}

bool dpc_queue_empty() {
    dpc_queue_t* queue = CPU_LOCAL_READ(dpc_queue);
    if(queue == NULL) return true;
    return queue->dpc_list.count == 0;
}

void dpc_execute_all() {
    assert(irql_get() == IRQL_DISPATCH);
    dpc_queue_t* queue = CPU_LOCAL_READ(dpc_queue);
    assert(queue != NULL);

    spinlock_lock(&queue->lock);

    while(!dpc_queue_empty()) {
        list_node_t* list_node = list_pop_front(&queue->dpc_list);
        dpc_t* dpc = CONTAINER_OF(list_node, dpc_t, list_node);
        dpc->handler(dpc, dpc->arg);
        dpc_destroy(dpc);
    }

    spinlock_unlock(&queue->lock);
}
