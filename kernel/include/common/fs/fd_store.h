#pragma once
#include <common/fs/vfs.h>
#include <memory/heap.h>

typedef struct {
    vfs_node_t* node;
    size_t cursor;
} fd_data_t;

typedef struct {
    fd_data_t** fds;
    size_t size;
    size_t capacity;
    spinlock_t lock;
} fd_store_t;

fd_store_t* fd_store_create();
void fd_store_free(fd_store_t* fd_store);

int fd_store_allocate(fd_store_t* fd_store, fd_data_t* node);
bool fd_store_close(fd_store_t* fd_store, size_t index);

fd_data_t* fd_store_get_fd(fd_store_t* fd_store, size_t index);
void fd_store_set_fd(fd_store_t* fd_store, size_t index, fd_data_t* node);
