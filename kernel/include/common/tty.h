#include <lib/buffer.h>
#include <lib/linked_list.h>
#include <stdint.h>

typedef struct {
    spinlock_t lock;
    buffer_t* input_buffer;
    list_t wait_queue;
    uint64_t owner_process_group_id;
    bool echo;
} tty_t;

extern tty_t* g_tty;

tty_t* tty_init();
void tty_put_byte(tty_t* tty, uint8_t byte);
char* tty_read(tty_t* tty, size_t* size);
