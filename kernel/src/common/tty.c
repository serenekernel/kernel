#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/tty.h>
#include <linked_list.h>
#include <memory/heap.h>
#include <spinlock.h>
#include <string.h>

#include "arch/cpu_local.h"
#include "buffer.h"

tty_t* g_tty;

tty_t* tty_init() {
    tty_t* tty = heap_alloc(sizeof(tty_t));
    tty->lock = SPINLOCK_INIT;
    tty->input_buffer = buffer_create(16);
    tty->wait_queue = LIST_INIT;
    tty->echo = true;
    return tty;
}

void tty_put_byte(tty_t* tty, uint8_t byte) {
    spinlock_lock(&tty->lock);
    buffer_append(tty->input_buffer, &byte, 1);
    if(byte == '\n') {
        while(tty->wait_queue.count > 0) {
            list_node_t* node = list_pop_front(&tty->wait_queue);
            thread_t* thread = CONTAINER_OF(node, thread_t, list_node_wait);
            sched_thread_schedule(thread);
        }
    } else if(byte == '\b' && tty->input_buffer->size > 0) {
        tty->input_buffer->size--;
    } else if(byte == ('u' & 0x1f)) {
        buffer_clear(tty->input_buffer);
    }
    spinlock_unlock(&tty->lock);
    if(tty->echo) printf("%c", byte);
}

char* tty_read(tty_t* tty, size_t* size) {
    if(size == NULL) { return NULL; }
    if(CPU_LOCAL_GET_CURRENT_THREAD()->common.process->process_group_pid != tty->owner_process_group_id) {
        *size = 0;
        return NULL;
    }
    spinlock_lock(&tty->lock);
    while(tty->input_buffer->size == 0) {
        thread_t* current = &CPU_LOCAL_READ(current_thread)->common;
        list_push_back(&tty->wait_queue, &current->list_node_wait);
        sched_yield(THREAD_STATE_BLOCKED);
    }

    char* data = heap_alloc(tty->input_buffer->size + 1);
    memcpy(data, tty->input_buffer->data, tty->input_buffer->size);
    data[tty->input_buffer->size] = '\0';
    *size = tty->input_buffer->size;

    buffer_clear(tty->input_buffer);
    spinlock_unlock(&tty->lock);
    return data;
}
