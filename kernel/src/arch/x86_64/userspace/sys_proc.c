#include <arch/cpu_local.h>
#include <arch/msr.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/sys_proc.h>
#include <common/userspace/userspace.h>
#include <stdio.h>

syscall_ret_t syscall_sys_exit(uint64_t exit_code) {
    LOG_INFO("syscall_sys_exit: pid=%lu, exit_code=%lu\n", CPU_LOCAL_GET_CURRENT_THREAD()->common.process->pid, exit_code);
    sched_yield(THREAD_STATE_DEAD);
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_tcb_set(virt_addr_t pointer) {
    write_msr(IA32_FS_BASE_MSR, pointer);
    return SYSCALL_RET_VALUE(0);
}


#define SYSCALL_GET_PROC_INFO_PID 0
#define SYSCALL_GET_PROC_INFO_PPID 1
#define SYSCALL_GET_PROC_INFO_GID 2
#define SYSCALL_GET_PROC_INFO_EGID 3
#define SYSCALL_GET_PROC_INFO_UID 4
#define SYSCALL_GET_PROC_INFO_EUID 5
#define SYSCALL_GET_PROC_INFO_GET_PGID 6
#define SYSCALL_GET_PROC_INFO_SET_PGID 7

syscall_ret_t __get_pgid(uint64_t pid) {
    process_t* process;
    if(pid == 0) {
        process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    } else {
        process = process_get_by_pid(pid);
    }

    if(process == NULL) { return SYSCALL_RET_ERROR(ERROR_INVAL); }

    return SYSCALL_RET_VALUE(process->process_group_pid);
}

syscall_ret_t __set_pgid(uint64_t pid, uint64_t pgid) {
    process_t* process;
    if(pid == 0) {
        process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    } else {
        process = process_get_by_pid(pid);
    }

    if(process == NULL) { return SYSCALL_RET_ERROR(ERROR_INVAL); }

    // @todo: make this not fucking lazy
    process->process_group_pid = pgid;
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_get_proc_info(uint64_t query, uint64_t param1, uint64_t param2) {
    // @todo: process group id is lazy as fuck and not posix compliant
    switch(query) {
        case SYSCALL_GET_PROC_INFO_PID:      return SYSCALL_RET_VALUE(CPU_LOCAL_GET_CURRENT_THREAD()->common.process->pid);
        case SYSCALL_GET_PROC_INFO_PPID:     return SYSCALL_RET_VALUE(0);
        case SYSCALL_GET_PROC_INFO_GID:      return SYSCALL_RET_VALUE(0);
        case SYSCALL_GET_PROC_INFO_EGID:     return SYSCALL_RET_VALUE(0);
        case SYSCALL_GET_PROC_INFO_UID:      return SYSCALL_RET_VALUE(0);
        case SYSCALL_GET_PROC_INFO_EUID:     return SYSCALL_RET_VALUE(0);
        case SYSCALL_GET_PROC_INFO_GET_PGID: return __get_pgid(param1);
        case SYSCALL_GET_PROC_INFO_SET_PGID: return __set_pgid(param1, param2);
        default:                             return SYSCALL_RET_ERROR(ERROR_INVAL);
    }
}
