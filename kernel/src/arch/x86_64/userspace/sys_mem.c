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
    printf("vm_map: hint=%p size=%zu prot=0x%lx flags=0x%lx fd=%d offset=0x%lx\n", hint, size, prot, flags, fd, offset);
    SYSCALL_ASSERT_PARAM(size > 0 && size <= (1ULL << 30));
    SYSCALL_ASSERT_PARAM((flags & (MAP_FIXED | MAP_FILE)) == 0);
    SYSCALL_ASSERT_PARAM(prot != PROT_NONE);

    vm_flags_t vm_flags = VM_READ_ONLY;
    if(prot & PROT_WRITE) { vm_flags |= VM_READ_WRITE; }
    if(prot & PROT_EXEC) { vm_flags |= VM_EXECUTE; }

    uintptr_t vaddr = vmm_alloc_backed(current_process->allocator, ALIGN_UP(size, PAGE_SIZE_DEFAULT) / PAGE_SIZE_DEFAULT, VM_ACCESS_USER, VM_CACHE_NORMAL, vm_flags, true);

    return SYSCALL_RET_VALUE(vaddr);
}

syscall_ret_t syscall_sys_vm_unmap(uint64_t addr, size_t size) {
    (void) size;
    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    SYSCALL_ASSERT_PARAM(addr != 0);
    vmm_free(current_process->allocator, addr);
    return SYSCALL_RET_VALUE(0);
}
