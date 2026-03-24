#include <arch/cpu_local.h>
#include <arch/msr.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/sys_proc.h>
#include <common/userspace/userspace.h>
#include <stdio.h>

#include "memory/vmm.h"

syscall_ret_t syscall_sys_mem_anon_alloc(uint64_t size, uint64_t align) {
    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    SYSCALL_ASSERT_PARAM(size > 0 && size <= (1ULL << 30));

    virt_addr_t vaddr = vmm_alloc_aligned_bytes(current_process->allocator, size, align);
    if(!vaddr) { return SYSCALL_RET_ERROR(SYSCALL_ERR_OUT_OF_MEMORY); }

    return SYSCALL_RET_VALUE(vaddr);
}

syscall_ret_t syscall_sys_mem_anon_free(uint64_t addr) {
    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    SYSCALL_ASSERT_PARAM(addr != 0);

    vmm_free(current_process->allocator, addr);
    return SYSCALL_RET_VALUE(0);
}
