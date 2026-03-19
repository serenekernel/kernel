#pragma once

#include <assert.h>
#include <lib/math.h>
#include <memory/heap.h>
#include <memory/pmm.h>
#include <memory/slab.h>

void* heap_alloc(size_t size);
void* heap_realloc(void* address, size_t current_size, size_t new_size);
void* heap_reallocarray(void* array, size_t element_size, size_t current_count, size_t new_count);
void heap_free(void* address, size_t size);

void init_heap();
