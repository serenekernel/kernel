#pragma once

#include "memory/memory.h"

#include <common/spinlock.h>
#include <lib/linked_list.h>
#include <stddef.h>
#include <stdint.h>

#define MAGAZINE_SIZE 32
#define MAGAZINE_EXTRA (arch_get_core_count() * 4)

typedef struct slab slab_t;
typedef struct slab_magazine slab_magazine_t;

struct slab_magazine {
    void* objects[MAGAZINE_SIZE];
    size_t rounds;

    list_node_t list_node;
};

typedef struct slab_cpu_cache {
    spinlock_t lock;

    slab_magazine_t* primary;
    slab_magazine_t* secondary;
} slab_cpu_cache_t;

typedef struct slab_cache {
    const char* name;
    size_t object_size;
    size_t slab_size;
    size_t slab_align;

    list_node_t list_node;

    spinlock_t slab_lock;
    list_t slab_full_list;
    list_t slab_partial_list;

    spinlock_t magazine_lock;
    list_t magazine_full_list;
    list_t magazine_empty_list;

    bool cpu_cached;
    slab_cpu_cache_t cpu_cache[];
} slab_cache_t;

struct slab {
    virt_addr_t buffer;

    size_t free_count;
    void* free_list;

    slab_cache_t* cache;

    list_node_t list_node;
};

slab_cache_t* slab_cache_create(const char* name, size_t object_size, size_t alignment);
void slab_cache_destroy(slab_cache_t* cache);

void slab_cache_init();

void* slab_cache_alloc(slab_cache_t* cache);
void slab_cache_free(slab_cache_t* cache, void* ptr);
