#include <assert.h>
#include <common/arch.h>
#include <common/irql.h>
#include <lib/spinlock.h>
#include <linked_list.h>
#include <memory/memory.h>
#include <memory/slab.h>
#include <memory/vm.h>

static spinlock_t g_slab_caches_lock = SPINLOCK_INIT;
static list_t g_slab_caches = LIST_INIT;

static slab_cache_t g_alloc_cache;
static slab_cache_t g_alloc_magazine;

slab_t* slab_cache_create_slab(slab_cache_t* cache) {
    // @todo: holy fucking memory waste
    virt_addr_t block = ALIGN_UP(vm_map_anon(g_global_address_space, VM_NO_HINT, cache->slab_size + cache->slab_align, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_ZERO), cache->slab_align);

    slab_t* slab = (slab_t*) block;
    slab->cache = cache;
    slab->buffer = block;
    slab->free_list = nullptr;
    slab->free_count = 0;

    size_t slab_size = cache->slab_size - sizeof(slab_t);
    assert(slab_size / cache->object_size > 0);
    for(size_t i = 0; i < slab_size / cache->object_size; i++) {
        void** obj = (void**) (((uintptr_t) slab) + sizeof(slab_t) + (i * cache->object_size));
        *obj = slab->free_list;
        slab->free_list = obj;
        assert(slab->free_list != nullptr);
        slab->free_count++;
    }
    return slab;
}

void slab_cache_destroy_slab(slab_cache_t* cache, slab_t* slab) {
    (void) cache;
    (void) slab;
    // @todo
}

void* slab_cache_alloc_from_slab(slab_cache_t* cache) {
    spinlock_lock(&cache->slab_lock);

    if(cache->slab_partial_list.count == 0) {
        spinlock_unlock(&cache->slab_lock);
        slab_t* new_slab = slab_cache_create_slab(cache);
        spinlock_lock(&cache->slab_lock);
        list_push(&cache->slab_partial_list, &new_slab->list_node);
    }

    assert(cache->slab_partial_list.head);
    slab_t* slab = CONTAINER_OF(cache->slab_partial_list.head, slab_t, list_node);
    assert(slab->free_count > 0);

    void* ptr = slab->free_list;
    slab->free_list = *(void**) ptr;
    slab->free_count--;
    if(slab->free_count == 0) {
        list_node_delete(&cache->slab_partial_list, &slab->list_node);
        list_push(&cache->slab_full_list, &slab->list_node);
    }

    spinlock_unlock(&cache->slab_lock);
    return ptr;
}

void slab_cache_free_to_slab(slab_cache_t* cache, void* ptr) {
    slab_t* slab = (slab_t*) ALIGN_DOWN(ptr, cache->slab_align);

    spinlock_lock(&cache->slab_lock);

    if(slab->free_count == 0) {
        list_node_delete(&cache->slab_full_list, &slab->list_node);
        list_push(&cache->slab_partial_list, &slab->list_node);
    }

    *(void**) ptr = slab->free_list;
    assert(ptr != nullptr);
    slab->free_list = ptr;
    slab->free_count++;

    spinlock_unlock(&cache->slab_lock);
}

slab_cache_t* slab_cache_create(const char* name, size_t object_size, size_t alignment) {
    assert(object_size >= 8 && "object_size must be greater than 8 bytes");
    assert(alignment > 0 && "alignment must be greater than 0");

    slab_cache_t* cache = slab_cache_alloc_from_slab(&g_alloc_cache);
    cache->name = name;
    cache->object_size = object_size;
    cache->slab_size = PAGE_SIZE_DEFAULT * 4;
    cache->slab_align = PAGE_SIZE_DEFAULT * 4;
    cache->cpu_cached = true;

    cache->slab_lock = SPINLOCK_INIT;
    cache->slab_full_list = LIST_INIT;
    cache->slab_partial_list = LIST_INIT;

    cache->magazine_lock = SPINLOCK_INIT;
    cache->magazine_full_list = LIST_INIT;
    cache->magazine_empty_list = LIST_INIT;

    for(size_t i = 0; i < MAGAZINE_EXTRA; i++) {
        slab_magazine_t* magazine = slab_cache_alloc_from_slab(&g_alloc_magazine);
        magazine->rounds = 0;
        for(size_t j = 0; j < MAGAZINE_SIZE; j++) magazine->objects[j] = nullptr;
        list_push(&cache->magazine_empty_list, &magazine->list_node);
    }

    if(cache->cpu_cached) {
        for(size_t i = 0; i < arch_get_core_count(); i++) {
            slab_magazine_t* magazine_primary = slab_cache_alloc_from_slab(&g_alloc_magazine);
            magazine_primary->rounds = MAGAZINE_SIZE;
            for(size_t j = 0; j < MAGAZINE_SIZE; j++) magazine_primary->objects[j] = slab_cache_alloc_from_slab(cache);

            slab_magazine_t* magazine_secondary = slab_cache_alloc_from_slab(&g_alloc_magazine);
            magazine_secondary->rounds = 0;
            for(size_t j = 0; j < MAGAZINE_SIZE; j++) magazine_secondary->objects[j] = nullptr;

            cache->cpu_cache[i].lock = SPINLOCK_INIT;
            cache->cpu_cache[i].primary = magazine_primary;
            cache->cpu_cache[i].secondary = magazine_secondary;
        }
    }

    spinlock_lock(&g_slab_caches_lock);
    list_push(&g_slab_caches, &cache->list_node);
    spinlock_unlock(&g_slab_caches_lock);

    return cache;
}

void slab_cache_destroy(slab_cache_t* cache) {
    (void) cache;
    // @todo
}

void* slab_cache_alloc(slab_cache_t* cache) {
    if(!cache->cpu_cached) { return slab_cache_alloc_from_slab(cache); }

    irql_t __irql = irql_raise(IRQL_DISPATCH);
    slab_cpu_cache_t* cc = &cache->cpu_cache[arch_get_core_id()];

    spinlock_lock(&cc->lock);

    if(cc->primary->rounds > 0) {
        void* ptr = cc->primary->objects[--cc->primary->rounds];
        spinlock_unlock(&cc->lock);
        irql_lower(__irql);
        return ptr;
    }

    if(cc->secondary->rounds == MAGAZINE_SIZE) {
        slab_magazine_t* temp = cc->secondary;
        cc->secondary = cc->primary;
        cc->primary = temp;

        void* ptr = cc->primary->objects[--cc->primary->rounds];
        spinlock_unlock(&cc->lock);
        irql_lower(__irql);
        return ptr;
    }

    spinlock_lock(&cache->magazine_lock);

    if(cache->magazine_full_list.count != 0) {
        list_push(&cache->magazine_empty_list, &cc->secondary->list_node);
        cc->secondary = cc->primary;
        cc->primary = CONTAINER_OF(list_pop_front(&cache->magazine_full_list), slab_magazine_t, list_node);
        spinlock_unlock(&cache->magazine_lock);
        void* ptr = cc->primary->objects[--cc->primary->rounds];
        spinlock_unlock(&cc->lock);
        irql_lower(__irql);
        return ptr;
    }

    spinlock_unlock(&cache->magazine_lock);
    spinlock_unlock(&cc->lock);

    void* ptr = slab_cache_alloc_from_slab(cache);
    irql_lower(__irql);

    return ptr;
}

void slab_cache_free(slab_cache_t* cache, void* ptr) {
    if(!cache->cpu_cached) {
        slab_cache_free_to_slab(cache, ptr);
        return;
    }


    irql_t __irql = irql_raise(IRQL_DISPATCH);
    slab_cpu_cache_t* cc = &cache->cpu_cache[arch_get_core_id()];

    spinlock_lock(&cc->lock);

    if(cc->primary->rounds < MAGAZINE_SIZE) {
        cc->primary->objects[cc->primary->rounds++] = ptr;
        spinlock_unlock(&cc->lock);
        irql_lower(__irql);
        return;
    }

    if(cc->secondary->rounds == 0) {
        slab_magazine_t* temp = cc->secondary;
        cc->secondary = cc->primary;
        cc->primary = temp;

        cc->primary->objects[cc->primary->rounds++] = ptr;
        spinlock_unlock(&cc->lock);
        irql_lower(__irql);
        return;
    }

    spinlock_lock(&cache->magazine_lock);

    if(cache->magazine_empty_list.count != 0) {
        list_push(&cache->magazine_full_list, &cc->secondary->list_node);
        cc->secondary = cc->primary;
        cc->primary = CONTAINER_OF(list_pop_front(&cache->magazine_empty_list), slab_magazine_t, list_node);
        spinlock_unlock(&cache->magazine_lock);

        cc->primary->objects[cc->primary->rounds++] = ptr;
        spinlock_unlock(&cc->lock);
        irql_lower(__irql);
        return;
    }

    spinlock_unlock(&cache->magazine_lock);
    spinlock_unlock(&cc->lock);

    slab_cache_free_to_slab(cache, ptr);
    irql_lower(__irql);
    return;
}

void slab_cache_init() {
    g_alloc_cache = (slab_cache_t) { .name = "slab-cache",
                                     .object_size = sizeof(slab_cache_t) + (arch_get_core_count() * sizeof(slab_cpu_cache_t)),
                                     .slab_size = PAGE_SIZE_DEFAULT * 4,
                                     .slab_align = PAGE_SIZE_DEFAULT * 4,
                                     .slab_lock = SPINLOCK_INIT,
                                     .slab_full_list = LIST_INIT,
                                     .slab_partial_list = LIST_INIT,
                                     .magazine_lock = SPINLOCK_INIT,
                                     .magazine_full_list = LIST_INIT,
                                     .magazine_empty_list = LIST_INIT,
                                     .cpu_cached = false };

    g_alloc_magazine = (slab_cache_t) { .name = "slab-magazine",
                                        .object_size = sizeof(slab_magazine_t) + (MAGAZINE_SIZE * sizeof(void*)),
                                        .slab_size = PAGE_SIZE_DEFAULT * 2,
                                        .slab_align = PAGE_SIZE_DEFAULT * 2,
                                        .slab_lock = SPINLOCK_INIT,
                                        .slab_full_list = LIST_INIT,
                                        .slab_partial_list = LIST_INIT,
                                        .magazine_lock = SPINLOCK_INIT,
                                        .magazine_full_list = LIST_INIT,
                                        .magazine_empty_list = LIST_INIT,
                                        .cpu_cached = false };

    list_push(&g_slab_caches, &g_alloc_cache.list_node);
    list_push(&g_slab_caches, &g_alloc_magazine.list_node);
}
