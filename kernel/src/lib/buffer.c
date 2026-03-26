#include <lib/buffer.h>
#include <memory/heap.h>
#include <string.h>

buffer_t buffer_create(size_t max_size) {
    buffer_t buffer = { 0 };
    buffer.data = heap_alloc(max_size);
    buffer.max_size = max_size;
    return buffer;
}

void buffer_free(buffer_t* buffer) {
    heap_free(buffer->data, buffer->max_size);
    buffer->data = NULL;
    buffer->size = 0;
    buffer->max_size = 0;
}

void buffer_append(buffer_t* buffer, const void* data, size_t size) {
    if(buffer->size + size > buffer->max_size) {
        buffer->data = heap_realloc(buffer->data, buffer->max_size, buffer->max_size * 2);
        buffer->max_size *= 2;
    }

    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
}
