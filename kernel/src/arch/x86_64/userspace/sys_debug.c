#include <arch/cpu_local.h>
#include <common/arch.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/userspace/sys_proc.h>
#include <common/userspace/userspace.h>
#include <memory/memory.h>
#include <stdio.h>

syscall_ret_t syscall_sys_debug_log(virt_addr_t buf, size_t count) {
    if(count == 0) { return SYSCALL_RET_VALUE(0); }
    if(!validate_user_buffer(CPU_LOCAL_GET_CURRENT_THREAD()->common.process, buf, count)) { return SYSCALL_RET_ERROR(ERROR_FAULT); }

    bool __prev = arch_disable_uap();
    printf(COLORIZE("dbgl | ", "34") "%.*s\n", (int) count, (const char*) buf);
    arch_restore_uap(__prev);
    return SYSCALL_RET_VALUE(0);
}
