#include <common/userspace/userspace.h>
#include <memory/memory.h>
#include <memory/vmm.h>

bool validate_user_buffer(process_t* proc, virt_addr_t addr, size_t size) {
    if(addr < proc->allocator->start || addr + size > proc->allocator->end) { return false; }

    // loop each page
    for(virt_addr_t i = addr; i < addr + size; i += PAGE_SIZE_DEFAULT) {
        vm_flags_t out_protection;
        vm_access_t out_access;
        phys_addr_t phys = vm_resolve_protections(proc->allocator, i, &out_protection, &out_access);
        if(phys == 0) { return false; }
        if(out_access != VM_ACCESS_USER) { return false; }
    }

    return true;
}
