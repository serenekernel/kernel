#pragma once
#include <linked_list.h>

typedef struct thread thread_t;
typedef enum thread_state thread_state_t;

enum thread_state {
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_DEAD,
};

struct thread {
    uint32_t tid;
    thread_state_t state;

    struct scheduler* sched;
    list_node_t list_node_sched;
};
