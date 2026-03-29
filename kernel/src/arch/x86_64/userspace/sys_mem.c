#include <arch/cpu_local.h>
#include <arch/msr.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/sys_proc.h>
#include <common/userspace/userspace.h>
#include <memory/memory.h>
#include <memory/vm.h>
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

    vm_protection_t vm_prot;
    if(prot & PROT_READ) { vm_prot.read = true; }
    if(prot & PROT_WRITE) { vm_prot.write = true; }
    if(prot & PROT_EXEC) { vm_prot.execute = true; }

    uint64_t vm_flags = VM_FLAG_NONE;
    if(flags & MAP_FIXED) { vm_flags |= VM_FLAG_FIXED; }

    virt_addr_t vaddr = (virt_addr_t) vm_map_anon(current_process->address_space, (void*) hint, ALIGN_UP(size, PAGE_SIZE_DEFAULT), vm_prot, VM_CACHE_NORMAL, vm_flags);
    if(vaddr == 0) { return SYSCALL_RET_ERROR(ERROR_INVAL); }
    return SYSCALL_RET_VALUE(vaddr);
}

syscall_ret_t syscall_sys_vm_unmap(uint64_t addr, size_t size) {
    (void) size;
    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    SYSCALL_ASSERT_PARAM(addr != 0);
    vm_unmap(current_process->address_space, (void*) addr, size);
    return SYSCALL_RET_VALUE(0);
}


syscall_ret_t syscall_sys_vm_protect(uint64_t addr, size_t size, size_t prot) {
    vm_protection_t vm_prot;
    if(prot & PROT_READ) { vm_prot.read = true; }
    if(prot & PROT_WRITE) { vm_prot.write = true; }
    if(prot & PROT_EXEC) { vm_prot.execute = true; }

    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    LOG_INFO("vm_protect: addr=%p size=%zu prot=%zu\n", addr, size, prot);

    vm_rewrite_prot(current_process->address_space, (void*) addr, size, vm_prot);
    return SYSCALL_RET_VALUE(0);
}
