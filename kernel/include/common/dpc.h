#pragma once
#include <common/spinlock.h>
#include <lib/linked_list.h>
#include <stdint.h>

typedef struct dpc dpc_t;
typedef void (*fn_dpc_handler_t)(struct dpc* dpc, void* arg);

typedef struct {
    spinlock_t lock;
    list_t dpc_list;
} dpc_queue_t;

struct dpc {
    fn_dpc_handler_t handler;
    void* arg;

    list_node_t list_node;
};

// @brief Initializes the DPC queue for this CPU.
void dpc_init_queue();

// @brief Creates a new DPC object.
// @param handler The handler function to call when the DPC is executed.
// @return A pointer to the newly created DPC object, or NULL if allocation fails.
dpc_t* dpc_create(fn_dpc_handler_t handler);

// @brief Destroys a DPC object.
// @param dpc The DPC object to destroy.
void dpc_destroy(dpc_t* dpc);

// @brief Enqueues a DPC object to be executed on the current CPU.
// @param dpc The DPC object to enqueue.
// @param arg The argument to pass to the DPC handler.
void dpc_enqueue(dpc_t* dpc, void* arg);

// @brief Returns true if the DPC queue is empty.
bool dpc_queue_empty();

// @brief Executes all DPC objects in the CPUs queue.
void dpc_execute_all();
