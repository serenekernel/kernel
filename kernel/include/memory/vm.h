#pragma once
#include <lib/sparse_array.h>
#include <lib/spinlock.h>
#include <linked_list.h>
#include <memory/memory.h>
#include <rbtree.h>
#include <stddef.h>

// this shit stolen twin
// https://github.com/elysium-os/cronus/blob/ea9b48c99e29781aa5f6f30fb887c7a02b68e30e/include/memory/vm.h

typedef enum {
    VM_REGION_TYPE_ANON,
    VM_REGION_TYPE_DIRECT
} vm_region_type_t;

typedef enum {
    VM_FAULT_UKKNOWN = 0,
    VM_FAULT_NOT_PRESENT,
} vm_fault_reason_t;

#define VM_FLAG_NONE 0
#define VM_FLAG_FIXED (1 << 1)
#define VM_FLAG_DYNAMICALLY_BACKED (1 << 2)
#define VM_FLAG_ZERO (1 << 10) /* only applies to anonymous mappings */

#define VM_NO_HINT 0

typedef struct vm_address_space vm_address_space_t;

typedef struct {
    vm_address_space_t* address_space;

    uintptr_t base;
    size_t length;

    vm_region_type_t type;
    vm_cache_t cache;

    vm_protection_t protection;
    bool dynamically_backed : 1;

    rb_node_t region_tree_node;
    list_node_t region_cache_node;

    union {
        struct {
            bool back_zeroed;
        } anon;
        struct {
            uintptr_t physical_address;
        } direct;
    } type_data;
} vm_region_t;

typedef struct {
    spinlock_t ptm_lock;
    phys_addr_t ptm_root;
} vm_ptm_t;

struct vm_address_space {
    bool is_user;
    vm_ptm_t ptm;

    virt_addr_t start, end;
    rb_tree_t regions_tree;
    spinlock_t lock;
};

#define VM_FLAG_NONE 0
#define VM_FLAG_FIXED (1 << 1)
#define VM_FLAG_DYNAMICALLY_BACKED (1 << 2)
#define VM_FLAG_ZERO (1 << 10) /* only applies to anonymous mappings */

typedef uint64_t vm_flags_t;

extern vm_address_space_t* g_global_address_space;

/// Map a region of anonymous memory.
/// @param hint Page aligned address
/// @param length Page aligned length
void* vm_map_anon(vm_address_space_t* address_space, void* hint, size_t length, vm_protection_t prot, vm_cache_t cache, vm_flags_t flags);

/// Map a region of direct memory.
/// @param hint Page aligned address
/// @param length Page aligned length
void* vm_map_direct(vm_address_space_t* address_space, void* hint, size_t length, vm_protection_t prot, vm_cache_t cache, uintptr_t physical_address, vm_flags_t flags);

/// Unmap a region of memory.
/// @param address Page aligned address
/// @param length Page aligned length
void vm_unmap(vm_address_space_t* address_space, void* address, size_t length);

/// Rewrite protection of a region of memory.
void vm_rewrite_prot(vm_address_space_t* address_space, void* address, size_t length, vm_protection_t prot);

/// Rewrite cacheability of a region of memory.
void vm_rewrite_cache(vm_address_space_t* address_space, void* address, size_t length, vm_cache_t cache);

/// Handle a virtual memory fault
/// @param fault Cause of the fault
/// @returns Is fault handled
bool vm_fault(uintptr_t address, vm_fault_reason_t fault);

/// Copy data to another address space.
size_t vm_copy_to(vm_address_space_t* dest_as, uintptr_t dest_addr, void* src, size_t count);

/// Copy data from another address space.
size_t vm_copy_from(void* dest, vm_address_space_t* src_as, uintptr_t src_addr, size_t count);

/// Create a regions rbtree.
rb_tree_t vm_create_regions();
