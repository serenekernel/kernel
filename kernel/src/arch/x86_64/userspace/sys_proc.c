#include <arch/cpu_local.h>
#include <arch/msr.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/sys_proc.h>
#include <common/userspace/userspace.h>
#include <stdio.h>

syscall_ret_t syscall_sys_exit(uint64_t exit_code) {
    printf("syscall_sys_exit: pid=%lu, exit_code=%lu\n", CPU_LOCAL_GET_CURRENT_THREAD()->common.process->pid, exit_code);
    sched_yield(THREAD_STATE_DEAD);
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_tcb_set(virt_addr_t pointer) {
    write_msr(IA32_FS_BASE_MSR, pointer);
    return SYSCALL_RET_VALUE(0);
}
