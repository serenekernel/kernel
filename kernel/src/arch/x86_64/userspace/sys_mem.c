#include <arch/cpu_local.h>
#include <arch/msr.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/sys_proc.h>
#include <common/userspace/userspace.h>
#include <memory/memory.h>
#include <memory/vmm.h>
#include <stdio.h>


#define PROT_NONE 0x00
#define PROT_READ 0x01
#define PROT_WRITE 0x02
#define PROT_EXEC 0x04

#define MAP_FILE 0x00
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANON 0x20

syscall_ret_t syscall_sys_vm_map(virt_addr_t hint, size_t size, size_t prot, size_t flags, size_t fd, size_t offset) {
    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    LOG_INFO("vm_map: hint=%p size=%zu prot=0x%lx flags=0x%lx fd=%d offset=0x%lx\n", hint, size, prot, flags, fd, offset);
    SYSCALL_ASSERT_PARAM(size > 0 && size <= (1ULL << 30));
    SYSCALL_ASSERT_PARAM((flags & (MAP_FILE)) == 0);

    // @todo: handle prot none

    vm_flags_t vm_flags = VM_READ_ONLY;
    if(prot & PROT_WRITE) { vm_flags |= VM_READ_WRITE; }
    if(prot & PROT_EXEC) { vm_flags |= VM_EXECUTE; }

    uintptr_t vaddr;
    if(flags & MAP_FIXED) {
        vaddr = vmm_alloc_fixed(current_process->allocator, hint, ALIGN_UP(size, PAGE_SIZE_DEFAULT) / PAGE_SIZE_DEFAULT, VM_ACCESS_USER, VM_CACHE_NORMAL, vm_flags, true);
        if(vaddr == 0) { return SYSCALL_RET_ERROR(ERROR_INVAL); }
    } else {
        vaddr = vmm_alloc_backed(current_process->allocator, ALIGN_UP(size, PAGE_SIZE_DEFAULT) / PAGE_SIZE_DEFAULT, VM_ACCESS_USER, VM_CACHE_NORMAL, vm_flags, true);
    }

    return SYSCALL_RET_VALUE(vaddr);
}

syscall_ret_t syscall_sys_vm_unmap(uint64_t addr, size_t size) {
    (void) size;
    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    SYSCALL_ASSERT_PARAM(addr != 0);
    vmm_free(current_process->allocator, addr);
    return SYSCALL_RET_VALUE(0);
}


syscall_ret_t syscall_sys_vm_protect(uint64_t addr, size_t size, size_t prot) {
    (void) size;
    uint64_t aligned_addr = ALIGN_DOWN(addr, PAGE_SIZE_DEFAULT);
    size_t aligned_size = ALIGN_UP(size, PAGE_SIZE_DEFAULT);

    vm_flags_t vm_flags = VM_READ_ONLY;
    if(prot & PROT_WRITE) { vm_flags |= VM_READ_WRITE; }
    if(prot & PROT_EXEC) { vm_flags |= VM_EXECUTE; }

    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    LOG_INFO("vm_protect: addr=%p size=%zu prot=%zu\n", aligned_addr, aligned_size, prot);
    if(aligned_size == 0) { return SYSCALL_RET_VALUE(0); }
    if(aligned_addr < current_process->allocator->start) {
        LOG_WARN("vm_protect: addr=%p is below start=%p\n", aligned_addr, current_process->allocator->start);
        return SYSCALL_RET_ERROR(ERROR_INVAL);
    }
    if(aligned_addr + aligned_size > current_process->allocator->end) {
        LOG_WARN("vm_protect: addr=%p is above end=%p\n", aligned_addr + aligned_size, current_process->allocator->end);
        return SYSCALL_RET_ERROR(ERROR_INVAL);
    }

    for(virt_addr_t i = aligned_addr; i < aligned_addr + aligned_size; i += PAGE_SIZE_DEFAULT) {
        phys_addr_t phys_addr;
        vm_flags_t protection;
        vm_access_t access;
        vm_resolve_protections(current_process->allocator, i, &phys_addr, &protection, &access);
        if(access == VM_ACCESS_KERNEL) { continue; }
        vm_reprotect_page(current_process->allocator, i, access, VM_CACHE_NORMAL, vm_flags);
    }

    return SYSCALL_RET_VALUE(0);
}
