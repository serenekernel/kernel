#include <assert.h>
#include <common/requests.h>
#include <memory/memory.h>
#include <memory/vmm.h>
#include <spinlock.h>
#include <string.h>

void vm_memset(vm_allocator_t* allocator, virt_addr_t base, int value, size_t size) {
    if(allocator->is_user == false) {
        memset((void*) base, value, size);
        return;
    }

    uintptr_t addr = base;
    size_t remaining = size;

    while(remaining > 0) {
        phys_addr_t phys;
        if(!vm_resolve(allocator, addr, &phys)) { assert(false && "tried to memset in unmapped user memory"); }

        size_t page_offset = addr % PAGE_SIZE_DEFAULT;
        size_t chunk = PAGE_SIZE_DEFAULT - page_offset;

        if(chunk > remaining) chunk = remaining;

        memset((void*) (TO_HHDM(phys) + page_offset), value, chunk);

        addr += chunk;
        remaining -= chunk;
    }
}

void* vm_memcpy(vm_allocator_t* dest_alloc, vm_allocator_t* src_alloc, virt_addr_t dest, virt_addr_t src, size_t size) {
    if(!dest_alloc->is_user && !src_alloc->is_user) {
        memcpy((void*) dest, (void*) src, size);
        return (void*) dest;
    }

    uintptr_t daddr = dest;
    uintptr_t saddr = src;
    size_t remaining = size;

    while(remaining > 0) {
        size_t dpage_offset = daddr % PAGE_SIZE_DEFAULT;
        size_t spage_offset = saddr % PAGE_SIZE_DEFAULT;

        size_t dchunk = PAGE_SIZE_DEFAULT - dpage_offset;
        size_t schunk = PAGE_SIZE_DEFAULT - spage_offset;
        size_t chunk = dchunk < schunk ? dchunk : schunk;

        if(chunk > remaining) chunk = remaining;

        void* dst_ptr;
        void* src_ptr;

        if(dest_alloc->is_user) {
            phys_addr_t dphys;
            if(!vm_resolve(dest_alloc, daddr, &dphys)) { assert(false && "tried to memcpy in unmapped user memory"); }
            dst_ptr = (void*) (TO_HHDM(dphys) + dpage_offset);
        } else {
            dst_ptr = (void*) daddr;
        }

        if(src_alloc->is_user) {
            phys_addr_t sphys;
            if(!vm_resolve(src_alloc, saddr, &sphys)) { assert(false && "tried to memcpy from unmapped user memory"); }
            src_ptr = (void*) (TO_HHDM(sphys) + spage_offset);
        } else {
            src_ptr = (void*) saddr;
        }

        memcpy(dst_ptr, src_ptr, chunk);

        daddr += chunk;
        saddr += chunk;
        remaining -= chunk;
    }
    return (void*) dest;
}
