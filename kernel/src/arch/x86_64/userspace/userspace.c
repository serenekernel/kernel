#include <common/userspace/userspace.h>
#include <memory/memory.h>
#include <memory/ptm.h>
#include <memory/vm.h>

bool validate_user_buffer(process_t* proc, virt_addr_t addr, size_t size) {
    if(addr < proc->address_space->start || addr + size > proc->address_space->end) { return false; }

    // loop each page
    for(virt_addr_t i = addr; i < addr + size; i += PAGE_SIZE_DEFAULT) {
        phys_addr_t phys;
        if(!ptm_physical(proc->address_space, i, &phys)) { return false; }
    }

    return true;
}
