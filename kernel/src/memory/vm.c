#include <arch/cpu_local.h>
#include <arch/hardware/lapic.h>
#include <assert.h>
#include <common/arch.h>
#include <common/requests.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <lib/spinlock.h>
#include <linked_list.h>
#include <math.h>
#include <memory/memory.h>
#include <memory/pmm.h>
#include <memory/ptm.h>
#include <memory/vm.h>
#include <rbtree.h>
#include <string.h>

#include "common/dpc.h"

vm_address_space_t* g_global_address_space;

typedef enum {
    REWRITE_TYPE_DELETE,
    REWRITE_TYPE_PROTECTION,
    REWRITE_TYPE_CACHE
} rewrite_type_t;

size_t vm_value_of_node(rb_node_t* node) {
    return CONTAINER_OF(node, vm_region_t, region_tree_node)->base;
}

size_t vm_length_of_node(rb_node_t* node) {
    return CONTAINER_OF(node, vm_region_t, region_tree_node)->length;
}

static spinlock_t g_region_cache_lock = SPINLOCK_INIT;
static list_t g_region_cache = LIST_INIT;

[[clang::always_inline]] bool address_in_bounds(virt_addr_t address, virt_addr_t start, virt_addr_t end) {
    return (address >= start) && (address < end);
}

[[clang::always_inline]] bool segment_in_bounds(virt_addr_t base, size_t length, virt_addr_t start, virt_addr_t end) {
    return (address_in_bounds(base, start, end) && (end - base) >= length);
}

[[clang::always_inline]] bool address_in_segment(virt_addr_t address, virt_addr_t base, size_t length) {
    return (address >= base && address < (base + length));
}

[[clang::always_inline]] bool segment_intersects(virt_addr_t base1, size_t length1, virt_addr_t base2, size_t length2) {
    return (base1 < (base2 + length2) && base2 < (base1 + length1));
}

[[clang::always_inline]] bool prot_equals(const vm_protection_t p1, const vm_protection_t p2) {
    return p1.read == p2.read && p1.write == p2.write && p1.execute == p2.execute;
}

static vm_region_t* region_insert(vm_address_space_t* address_space, vm_region_t* region);

/// Find last region within a segment.
static vm_region_t* find_region(vm_address_space_t* address_space, uintptr_t address, size_t length) {
    rb_node_t* node = rb_find(&address_space->regions_tree, address + length, RB_SEARCH_TYPE_NEAREST_LT);
    if(node == nullptr) return nullptr;

    vm_region_t* region = CONTAINER_OF(node, vm_region_t, region_tree_node);
    if(region->base + region->length > address) return region;

    return nullptr;
}

/// Find a hole (free space) in an address space.
/// @warning Assumes address space lock is acquired.
/// @returns true = hole found, false = not found
static bool find_hole(vm_address_space_t* address_space, uintptr_t address, size_t length, uintptr_t* hole) {
    if(segment_in_bounds(address, length, address_space->start, address_space->end) && find_region(address_space, address, length) == nullptr) {
        *hole = address;
        return true;
    }

    address = address_space->start;
    while(true) {
        if(!segment_in_bounds(address, length, address_space->start, address_space->end)) return false;

        vm_region_t* region = find_region(address_space, address, length);
        if(region == nullptr) {
            *hole = address;
            return true;
        }

        address = region->base + region->length;
    }
    return false;
}

static void region_map(vm_region_t* region, uintptr_t address, uintptr_t length) {
    assert(address % PAGE_SIZE_DEFAULT == 0 && length % PAGE_SIZE_DEFAULT == 0);
    assert(address < region->base || address + length >= region->base);

    bool is_global = region->address_space == g_global_address_space;
    switch(region->type) {
        case VM_REGION_TYPE_ANON:
            for(size_t i = 0; i < length; i += PAGE_SIZE_DEFAULT) {
                uintptr_t virtual_address = address + i;
                uintptr_t physical_address = pmm_alloc_page(region->type_data.anon.back_zeroed ? PMM_FLAG_ZERO : PMM_FLAG_NONE);


                ptm_map(region->address_space, virtual_address, physical_address, PAGE_SIZE_DEFAULT, region->protection, region->cache, is_global ? VM_PRIVILEGE_KERNEL : VM_PRIVILEGE_USER, is_global);
            }
            break;
        case VM_REGION_TYPE_DIRECT:
            ptm_map(region->address_space, address, region->type_data.direct.physical_address + (address - region->base), length, region->protection, region->cache, is_global ? VM_PRIVILEGE_KERNEL : VM_PRIVILEGE_USER, is_global);
            break;
    }
}

static void region_unmap(vm_region_t* region, uintptr_t address, uintptr_t length) {
    assert(address % PAGE_SIZE_DEFAULT == 0 && length % PAGE_SIZE_DEFAULT == 0);
    assert(address >= region->base && address + length <= region->base + region->length);

    switch(region->type) {
        case VM_REGION_TYPE_ANON:
            // OPTIMIZE: invent page cache for anon regions because the current way is extremely slow
            // TODO: unmap phys mem
            break;
        case VM_REGION_TYPE_DIRECT: break;
    }
    ptm_unmap(region->address_space, address, length);
}

/// Check whether the flags of a region are compatible with each other.
static bool regions_mergeable(vm_region_t* left, vm_region_t* right) {
    if(left->type != right->type) return false;
    if(left->base + left->length != right->base) return false;
    if(!prot_equals(left->protection, right->protection)) return false;
    if(left->cache != right->cache) return false;
    if(left->dynamically_backed != right->dynamically_backed) return false;

    switch(left->type) {
        case VM_REGION_TYPE_ANON:
            if(left->type_data.anon.back_zeroed != right->type_data.anon.back_zeroed) return false;
            break;
        case VM_REGION_TYPE_DIRECT:
            if(!segment_in_bounds(left->type_data.direct.physical_address, left->length, 0, ARCH_MEM_PHYS_MAX)) return false;
            if(left->type_data.direct.physical_address + left->length != right->type_data.direct.physical_address) return false;
            break;
    }

    return true;
}

/// Allocate a region from the internal vm region pool.
static vm_region_t* region_alloc(bool global_lock_acquired) {
    spinlock_lock(&g_region_cache_lock);
    if(g_region_cache.count == 0) {
        spinlock_unlock(&g_region_cache_lock);

        phys_addr_t page = pmm_alloc_page(PMM_FLAG_ZERO);
        if(!global_lock_acquired) spinlock_lock(&g_global_address_space->lock);

        uintptr_t address;
        if(!find_hole(g_global_address_space, VM_NO_HINT, PAGE_SIZE_DEFAULT, &address)) assert(false && "out of global address space");

        ptm_map(g_global_address_space, address, page, PAGE_SIZE_DEFAULT, VM_PROT_RW, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true);

        vm_region_t* region = (vm_region_t*) address;
        region[0].address_space = g_global_address_space;
        region[0].type = VM_REGION_TYPE_ANON;
        region[0].base = address;
        region[0].length = PAGE_SIZE_DEFAULT;
        region[0].protection = VM_PROT_RW;
        region[0].cache = VM_CACHE_NORMAL;
        region[0].dynamically_backed = false;

        region_insert(g_global_address_space, &region[0]);
        if(!global_lock_acquired) spinlock_unlock(&g_global_address_space->lock);

        spinlock_lock(&g_region_cache_lock);
        for(unsigned int i = 1; i < PAGE_SIZE_DEFAULT / sizeof(vm_region_t); i++) list_push(&g_region_cache, &region[i].region_cache_node);
    }

    list_node_t* node = list_pop(&g_region_cache);
    spinlock_unlock(&g_region_cache_lock);
    return CONTAINER_OF(node, vm_region_t, region_cache_node);
}

/// Free region into the internal vm region pool.
static void region_free(vm_region_t* region) {
    spinlock_lock(&g_region_cache_lock);
    list_push(&g_region_cache, &region->region_cache_node);
    spinlock_unlock(&g_region_cache_lock);
}

/// Create a region by cloning another to a new region.
static vm_region_t* clone_to(bool global_lock_acquired, uintptr_t base, size_t length, vm_region_t* from) {
    vm_region_t* region = region_alloc(global_lock_acquired);
    region->type = from->type;
    region->address_space = from->address_space;
    region->cache = from->cache;
    region->protection = from->protection;
    region->dynamically_backed = from->dynamically_backed;

    switch(from->type) {
        case VM_REGION_TYPE_ANON: region->type_data.anon.back_zeroed = from->type_data.anon.back_zeroed; break;
        case VM_REGION_TYPE_DIRECT:
            uintptr_t new_physical_address = from->type_data.direct.physical_address;
            if(base > from->base) {
                assert(UINTPTR_MAX - new_physical_address > base - from->base);
                new_physical_address += base - from->base;
            } else {
                assert(new_physical_address > from->base - base);
                new_physical_address -= from->base - base;
            }
            region->type_data.direct.physical_address = new_physical_address;
            break;
    }

    region->base = base;
    region->length = length;
    return region;
}

static vm_region_t* region_insert(vm_address_space_t* address_space, vm_region_t* region) {
    rb_node_t* right_node = rb_find(&address_space->regions_tree, region->base, RB_SEARCH_TYPE_NEAREST_GT);
    if(right_node != nullptr) {
        vm_region_t* right = CONTAINER_OF(right_node, vm_region_t, region_tree_node);
        if(regions_mergeable(region, right)) {
            region->length += right->length;
            rb_remove(&address_space->regions_tree, &right->region_tree_node);
            region_free(right);
        }
    }
    rb_insert(&address_space->regions_tree, &region->region_tree_node);

    rb_node_t* left_node = rb_find(&address_space->regions_tree, region->base, RB_SEARCH_TYPE_NEAREST_LT);
    if(left_node != nullptr) {
        vm_region_t* left = CONTAINER_OF(left_node, vm_region_t, region_tree_node);
        if(regions_mergeable(left, region)) {
            left->length += region->length;
            rb_remove(&address_space->regions_tree, &region->region_tree_node);
            region_free(region);
            return left;
        }
    }

    return region;
}

static vm_region_t* addr_to_region(vm_address_space_t* address_space, uintptr_t address) {
    if(!address_in_bounds(address, address_space->start, address_space->end)) return nullptr;

    rb_node_t* node = rb_find(&address_space->regions_tree, address, RB_SEARCH_TYPE_NEAREST_LTE);
    if(node == nullptr) return nullptr;

    vm_region_t* region = CONTAINER_OF(node, vm_region_t, region_tree_node);
    if(!address_in_segment(address, region->base, region->length)) return nullptr;

    return region;
}

static bool address_space_fix_page(vm_address_space_t* address_space, uintptr_t vaddr) {
    vm_region_t* region = addr_to_region(address_space, vaddr);
    if(region == nullptr || !region->dynamically_backed) return false;
    region_map(region, MATH_FLOOR(vaddr, PAGE_SIZE_DEFAULT), PAGE_SIZE_DEFAULT);
    return true;
}

static bool memory_exists(vm_address_space_t* address_space, uintptr_t address, size_t length) {
    if(!address_in_bounds(address, address_space->start, address_space->end) || !address_in_bounds(address + length, address_space->start, address_space->end)) return false;

    uintptr_t top = address + length;
    while(top > address) {
        rb_node_t* node = rb_find(&address_space->regions_tree, top, RB_SEARCH_TYPE_NEAREST_LT);
        if(node == nullptr) return false;

        vm_region_t* region = CONTAINER_OF(node, vm_region_t, region_tree_node);
        if(region->base + region->length < top) return false;

        top = region->base;
    }
    return true;
}


static void* map_common(vm_address_space_t* address_space, void* hint, size_t length, vm_protection_t prot, vm_cache_t cache, vm_flags_t flags, vm_region_type_t type, uintptr_t direct_physical_address) {
    uintptr_t address = (uintptr_t) hint;
    assert(address_space != nullptr);
    if((flags & VM_FLAG_FIXED) && address % PAGE_SIZE_DEFAULT != 0) {
        assert(false && "hint must be page aligned when VM_FLAG_FIXED is set");
        return nullptr;
    }
    if(length == 0 || length % PAGE_SIZE_DEFAULT != 0) {
        assertf(false, "length must be non-zero and page aligned %d", length);
        return nullptr;
    }
    if(address % PAGE_SIZE_DEFAULT != 0) { address += PAGE_SIZE_DEFAULT - (address % PAGE_SIZE_DEFAULT); }

    vm_region_t* region = region_alloc(false);
    spinlock_lock(&address_space->lock);

    if(!(flags & VM_FLAG_FIXED)) {
        bool result = find_hole(address_space, address, length, &address);
        if(!result) {
            region_free(region);
            spinlock_unlock(&address_space->lock);
            return nullptr;
        }
    }

    assert(segment_in_bounds(address, length, address_space->start, address_space->end));
    assert(address % PAGE_SIZE_DEFAULT == 0 && length % PAGE_SIZE_DEFAULT == 0);

    region->address_space = address_space;
    region->type = type;
    region->base = address;
    region->length = length;
    region->protection = prot;
    region->cache = cache;
    region->dynamically_backed = (flags & VM_FLAG_DYNAMICALLY_BACKED) != 0;

    switch(region->type) {
        case VM_REGION_TYPE_ANON: region->type_data.anon.back_zeroed = (flags & VM_FLAG_ZERO) != 0; break;
        case VM_REGION_TYPE_DIRECT:
            assert(direct_physical_address % PAGE_SIZE_DEFAULT == 0);
            region->type_data.direct.physical_address = direct_physical_address;
            break;
    }

    if(!region->dynamically_backed) region_map(region, region->base, region->length);

    region_insert(address_space, region);
    spinlock_unlock(&address_space->lock);
    return (void*) address;
}

static void rewrite_common(vm_address_space_t* address_space, void* address, size_t length, rewrite_type_t type, vm_protection_t prot, vm_cache_t cache) {
    if(length == 0) return;

    assert((uintptr_t) address % PAGE_SIZE_DEFAULT == 0);
    assert(length % PAGE_SIZE_DEFAULT == 0);
    assert(segment_in_bounds((uintptr_t) address, length, address_space->start, address_space->end));

    spinlock_lock(&address_space->lock);

    uintptr_t current_address = (uintptr_t) address;

    rb_node_t* node = rb_find(&address_space->regions_tree, current_address, RB_SEARCH_TYPE_NEAREST_LTE);
    if(node != nullptr) {
        vm_region_t* split_region = CONTAINER_OF(node, vm_region_t, region_tree_node);
        if(split_region->base + split_region->length > current_address) {
            uintptr_t split_base = current_address;
            size_t split_length = (split_region->base + split_region->length) - split_base;
            if(split_length > length) split_length = length;

            switch(type) {
                case REWRITE_TYPE_DELETE: region_unmap(split_region, split_base, split_length); goto l_no_clone;
                case REWRITE_TYPE_CACHE:
                    if(split_region->cache == cache) goto l_skip;
                    break;
                case REWRITE_TYPE_PROTECTION:
                    if(prot_equals(split_region->protection, prot)) goto l_skip;
                    break;
            }

            vm_region_t* region = clone_to(address_space == g_global_address_space, split_base, split_length, split_region);

        l_no_clone:

            if(split_region->base + split_region->length > split_base + split_length) {
                uintptr_t new_base = split_base + split_length;
                size_t new_length = (split_region->base + split_region->length) - (split_base + split_length);
                rb_insert(&address_space->regions_tree, &clone_to(address_space == g_global_address_space, new_base, new_length, split_region)->region_tree_node);
            }

            if(split_base > split_region->base) {
                split_region->length = split_base - split_region->base;
            } else {
                rb_remove(&address_space->regions_tree, &split_region->region_tree_node);
                region_free(split_region);
            }

            switch(type) {
                case REWRITE_TYPE_DELETE:     goto l_skip;
                case REWRITE_TYPE_CACHE:      region->cache = cache; break;
                case REWRITE_TYPE_PROTECTION: region->protection = prot; break;
            }

            region = region_insert(address_space, region);

            bool is_global = region->address_space == g_global_address_space;
            ptm_rewrite(region->address_space, split_base, split_length, region->protection, region->cache, is_global ? VM_PRIVILEGE_KERNEL : VM_PRIVILEGE_USER, is_global);

        l_skip:

            current_address = split_base + split_length;
        }
    }

    while(current_address < ((uintptr_t) address + length)) {
        node = rb_find(&address_space->regions_tree, current_address, RB_SEARCH_TYPE_NEAREST_GTE);
        if(node == nullptr) break;

        vm_region_t* split_region = CONTAINER_OF(node, vm_region_t, region_tree_node);
        current_address = split_region->base + split_region->length;

        size_t split_length = ((uintptr_t) address + length) - split_region->base;
        if(split_length > split_region->length) split_length = split_region->length;

        switch(type) {
            case REWRITE_TYPE_DELETE: region_unmap(split_region, split_region->base, split_length); goto r_no_clone;
            case REWRITE_TYPE_CACHE:
                if(split_region->cache == cache) goto r_skip;
                break;
            case REWRITE_TYPE_PROTECTION:
                if(prot_equals(split_region->protection, prot)) goto r_skip;
                break;
        }

        uintptr_t split_base = split_region->base;
        vm_region_t* region = clone_to(address_space == g_global_address_space, split_base, split_length, split_region);

    r_no_clone:

        if(split_region->length > split_length) {
            uintptr_t new_base = split_base + split_length;
            size_t new_length = split_region->length - split_length;
            rb_insert(&address_space->regions_tree, &clone_to(address_space == g_global_address_space, new_base, new_length, split_region)->region_tree_node);
        }
        rb_remove(&address_space->regions_tree, &split_region->region_tree_node);
        region_free(split_region);

        switch(type) {
            case REWRITE_TYPE_DELETE:     goto r_skip;
            case REWRITE_TYPE_CACHE:      region->cache = cache; break;
            case REWRITE_TYPE_PROTECTION: region->protection = prot; break;
        }

        region = region_insert(address_space, region);

        bool is_global = region->address_space == g_global_address_space;
        ptm_rewrite(region->address_space, split_base, split_length, region->protection, region->cache, is_global ? VM_PRIVILEGE_KERNEL : VM_PRIVILEGE_USER, is_global);

    r_skip:
    }
    spinlock_unlock(&address_space->lock);
}

void* vm_map_anon(vm_address_space_t* address_space, void* hint, size_t length, vm_protection_t prot, vm_cache_t cache, vm_flags_t flags) {
    return map_common(address_space, hint, length, prot, cache, flags, VM_REGION_TYPE_ANON, 0);
}

void* vm_map_direct(vm_address_space_t* address_space, void* hint, size_t length, vm_protection_t prot, vm_cache_t cache, uintptr_t physical_address, vm_flags_t flags) {
    return map_common(address_space, hint, length, prot, cache, flags, VM_REGION_TYPE_DIRECT, physical_address);
}

void vm_unmap(vm_address_space_t* address_space, void* address, size_t length) {
    rewrite_common(address_space, address, length, REWRITE_TYPE_DELETE, (vm_protection_t) {}, VM_CACHE_NORMAL);
}

void vm_rewrite_prot(vm_address_space_t* address_space, void* address, size_t length, vm_protection_t prot) {
    rewrite_common(address_space, address, length, REWRITE_TYPE_PROTECTION, prot, VM_CACHE_NORMAL);
}

void vm_rewrite_cache(vm_address_space_t* address_space, void* address, size_t length, vm_cache_t cache) {
    rewrite_common(address_space, address, length, REWRITE_TYPE_CACHE, (vm_protection_t) {}, cache);
}


size_t vm_copy_to(vm_address_space_t* dest_as, uintptr_t dest_addr, void* src, size_t count) {
    if(!memory_exists(dest_as, dest_addr, count)) return 0;
    size_t i = 0;
    while(i < count) {
        size_t offset = (dest_addr + i) % PAGE_SIZE_DEFAULT;
        uintptr_t phys;
        if(!ptm_physical(dest_as, dest_addr + i, &phys)) {
            if(!address_space_fix_page(dest_as, dest_addr + i)) return i;
            bool success = ptm_physical(dest_as, dest_addr + i, &phys);
            assert(success);
        }

        size_t len = MATH_MIN(count - i, PAGE_SIZE_DEFAULT - offset);
        memcpy((void*) TO_HHDM(phys), src, len);
        i += len;
        src += len;
    }
    return i;
}

size_t vm_copy_from(void* dest, vm_address_space_t* src_as, uintptr_t src_addr, size_t count) {
    if(!memory_exists(src_as, src_addr, count)) return 0;
    size_t i = 0;
    while(i < count) {
        size_t offset = (src_addr + i) % PAGE_SIZE_DEFAULT;
        uintptr_t phys;
        if(!ptm_physical(src_as, src_addr + i, &phys)) {
            if(!address_space_fix_page(src_as, src_addr + i)) return i;
            bool success = ptm_physical(src_as, src_addr + i, &phys);
            assert(success);
        }

        size_t len = MATH_MIN(count - i, PAGE_SIZE_DEFAULT - offset);
        memcpy(dest, (void*) TO_HHDM(phys), len);
        i += len;
        dest += len;
    }
    return i;
}

rb_tree_t vm_create_regions() {
    return ((rb_tree_t) { .value_of_node = vm_value_of_node, .length_of_node = vm_length_of_node, .root = nullptr });
}

static void vm_fault_soft(dpc_t* dpc, void* arg) {
    (void) dpc;
    x86_64_thread_t* thread = arg;

    assert(thread->common.process != nullptr);

    bool ok = address_space_fix_page(thread->common.process->address_space, thread->vm_fault.address);
    if(!ok) assertf(false, "vm_fault_soft handling failed for (pid: %lu, tid: %lu) on 0x%lx", thread->common.process->pid, thread->common.tid, thread->vm_fault.address);
    thread->vm_fault.in_flight = false;
    sched_thread_schedule(&thread->common);
}

bool vm_fault(uintptr_t address, vm_fault_reason_t fault) {
    if(fault != VM_FAULT_NOT_PRESENT) return false;
    if(address_in_bounds(address, g_global_address_space->start, g_global_address_space->end)) return false;

    x86_64_thread_t* current_thread = CPU_LOCAL_GET_CURRENT_THREAD();
    assert(!current_thread->vm_fault.in_flight);

    process_t* proc = current_thread->common.process;
    if(proc == nullptr) return false;

    dpc_t* dpc = dpc_create(vm_fault_soft, current_thread);

    current_thread->vm_fault.in_flight = true;
    current_thread->vm_fault.address = address;
    current_thread->vm_fault.dpc = dpc;

    dpc_enqueue(current_thread->vm_fault.dpc, current_thread);
    sched_yield(THREAD_STATE_BLOCKED);
    return true;
}
