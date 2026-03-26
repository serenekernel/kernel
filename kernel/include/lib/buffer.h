#include <memory/heap.h>

typedef struct {
    uint8_t* data;
    size_t size;
    size_t max_size;
} buffer_t;

buffer_t buffer_create(size_t max_size);
void buffer_free(buffer_t* buffer);
void buffer_append(buffer_t* buffer, const void* data, size_t size);
