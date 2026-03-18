#include "common/irql.h"
#include "common/spinlock.h"

#include <assert.h>
#include <common/cpu_local.h>
#include <common/dpc.h>
#include <memory/memory.h>
#include <memory/vmm.h>

void dpc_init_queue() {
    dpc_queue_t* queue = (dpc_queue_t*) vmm_alloc_object(&kernel_allocator, sizeof(dpc_queue_t));
    assert(queue != NULL);

    queue->head = NULL;
    queue->lock = SPINLOCK_INIT;
    CPU_LOCAL_WRITE(dpc_queue, queue);
}

dpc_t* dpc_create(fn_dpc_handler_t handler) {
    dpc_t* dpc = (dpc_t*) vmm_alloc_object(&kernel_allocator, sizeof(dpc_t));
    assert(dpc != NULL);

    dpc->handler = handler;
    dpc->next = NULL;
    dpc->prev = NULL;

    return dpc;
}

void dpc_destroy(dpc_t* dpc) {
    assert(dpc != NULL);

    vmm_free(&kernel_allocator, (virt_addr_t) dpc);
}

void dpc_enqueue(dpc_t* dpc, void* arg) {
    dpc->arg = arg;

    dpc_queue_t* queue = CPU_LOCAL_READ(dpc_queue);
    assert(queue != NULL);

    irql_t __irql = spinlock_lock(&queue->lock);

    dpc->next = queue->head;
    if(queue->head != NULL) { queue->head->prev = dpc; }
    queue->head = dpc;

    spinlock_unlock(&queue->lock, __irql);
}

bool dpc_queue_empty() {
    dpc_queue_t* queue = CPU_LOCAL_READ(dpc_queue);
    if(queue == NULL) return true;
    return queue->head == NULL;
}

void dpc_execute_all() {
    assert(irql_get() == IRQL_DISPATCH);
    dpc_queue_t* queue = CPU_LOCAL_READ(dpc_queue);
    assert(queue != NULL);

    irql_t __irql = spinlock_lock(&queue->lock);

    dpc_t* dpc = queue->head;
    while(dpc != NULL) {
        dpc_t* next = dpc->next;
        dpc->handler(dpc, dpc->arg);
        dpc_destroy(dpc);
        dpc = next;
    }
    queue->head = NULL;

    spinlock_unlock(&queue->lock, __irql);
}
