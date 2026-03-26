#include <common/fs/fd_store.h>
#include <memory/heap.h>
#include <spinlock.h>

#include "common/fs/vfs.h"

fd_store_t* fd_store_create() {
    fd_store_t* fd_store = heap_alloc(sizeof(fd_store_t));
    fd_store->fds = NULL;
    fd_store->size = 0;
    fd_store->capacity = 0;
    fd_store->lock = SPINLOCK_INIT;
    return fd_store;
}

void fd_store_free(fd_store_t* fd_store) {
    if(fd_store->fds) heap_free(fd_store->fds, fd_store->size * sizeof(fd_data_t*));
    heap_free(fd_store, sizeof(fd_store_t));
}


int fd_store_allocate(fd_store_t* fd_store, fd_data_t* node) {
    spinlock_lock(&fd_store->lock);
    for(size_t i = 0; i < fd_store->size; i++) {
        if(fd_store->fds[i] == NULL) {
            fd_store->fds[i] = node;
            spinlock_unlock(&fd_store->lock);
            return i;
        }
    }

    size_t index = fd_store->size;
    if(index >= fd_store->capacity) {
        size_t old_size = fd_store->capacity * sizeof(fd_data_t*);
        fd_store->capacity = index + 1;
        fd_store->fds = heap_realloc(fd_store->fds, old_size, fd_store->capacity * sizeof(fd_data_t*));
        assert(fd_store->fds != NULL);
    }
    fd_store->fds[index] = node;
    fd_store->size = index + 1;
    spinlock_unlock(&fd_store->lock);
    return index;
}

bool fd_store_close(fd_store_t* fd_store, size_t index) {
    if(index >= fd_store->size) { return false; }
    if(fd_store->fds[index] == NULL) { return false; }

    fd_data_t* node = fd_store->fds[index];
    fd_store->fds[index] = NULL;
    heap_free(node, sizeof(fd_data_t));
    return true;
}

void __fd_store_set_fd(fd_store_t* fd_store, size_t index, fd_data_t* node) {
    if(index >= fd_store->capacity) {
        size_t old_size = fd_store->capacity * sizeof(fd_data_t*);
        fd_store->capacity = index + 1;
        fd_store->fds = heap_realloc(fd_store->fds, old_size, fd_store->capacity * sizeof(fd_data_t*));
    }
    fd_store->fds[index] = node;
    if(index >= fd_store->size) fd_store->size = index + 1;
}

void fd_store_set_fd(fd_store_t* fd_store, size_t index, fd_data_t* node) {
    spinlock_lock(&fd_store->lock);
    __fd_store_set_fd(fd_store, index, node);
    spinlock_unlock(&fd_store->lock);
}

fd_data_t* fd_store_get_fd(fd_store_t* fd_store, size_t index) {
    if(index >= fd_store->size) return NULL;
    return fd_store->fds[index];
}

vfs_result_t fd_store_open(fd_store_t* fd_store, vfs_path_t* path) {
    vfs_node_t* node;
    vfs_result_t res = vfs_lookup(path, &node);
    if(res != VFS_RESULT_OK) return res;
    fd_data_t* fd_data = heap_alloc(sizeof(fd_data_t));
    if(fd_data == NULL) return VFS_RESULT_ERR_BUFFER_TOO_SMALL;
    fd_store_allocate(fd_store, fd_data);
    fd_data->node = node;
    fd_data->cursor = 0;
    return VFS_RESULT_OK;
}

vfs_result_t fd_store_open_fixed(fd_store_t* fd_store, vfs_path_t* path, int fd_num) {
    vfs_node_t* node;
    vfs_result_t res = vfs_lookup(path, &node);
    if(res != VFS_RESULT_OK) return res;
    fd_data_t* fd_data = heap_alloc(sizeof(fd_data_t));
    if(fd_data == NULL) return VFS_RESULT_ERR_BUFFER_TOO_SMALL;
    spinlock_lock(&fd_store->lock);
    fd_data_t* existing_fd_data = fd_store_get_fd(fd_store, fd_num);
    if(existing_fd_data != NULL) {
        spinlock_unlock(&fd_store->lock);
        return VFS_RESULT_ERR_EXISTS;
    }
    __fd_store_set_fd(fd_store, fd_num, fd_data);
    fd_data->node = node;
    fd_data->cursor = 0;
    spinlock_unlock(&fd_store->lock);
    return VFS_RESULT_OK;
}
