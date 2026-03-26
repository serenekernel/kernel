#include <arch/cpu_local.h>
#include <arch/msr.h>
#include <arch/sched/thread.h>
#include <assert.h>
#include <common/sched/process.h>
#include <common/userspace/sys_debug.h>
#include <common/userspace/sys_mem.h>
#include <common/userspace/sys_proc.h>
#include <common/userspace/sys_vfs.h>
#include <common/userspace/userspace.h>

#define SYSTRACE_ENABLED 0
void x86_64_handle_syscall();

typedef syscall_ret_t (*fn_syscall_handler0_t)();
typedef syscall_ret_t (*fn_syscall_handler1_t)(uint64_t);
typedef syscall_ret_t (*fn_syscall_handler2_t)(uint64_t, uint64_t);
typedef syscall_ret_t (*fn_syscall_handler3_t)(uint64_t, uint64_t, uint64_t);
typedef syscall_ret_t (*fn_syscall_handler4_t)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef syscall_ret_t (*fn_syscall_handler5_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef syscall_ret_t (*fn_syscall_handler6_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

typedef struct {
    size_t num_params;
    union {
        fn_syscall_handler0_t handler0;
        fn_syscall_handler1_t handler1;
        fn_syscall_handler2_t handler2;
        fn_syscall_handler3_t handler3;
        fn_syscall_handler4_t handler4;
        fn_syscall_handler5_t handler5;
        fn_syscall_handler6_t handler6;
    } handlers;
} syscall_entry_t;

#define MAX_SYSCALL_NUMBER 512

syscall_entry_t syscall_table[MAX_SYSCALL_NUMBER];


const char* convert_syscall_number(syscall_nr_t nr) {
    switch(nr) {
        case SYS_EXIT:           return "SYS_EXIT";
        case SYS_OPEN:           return "SYS_OPEN";
        case SYS_READ:           return "SYS_READ";
        case SYS_WRITE:          return "SYS_WRITE";
        case SYS_CLOSE:          return "SYS_CLOSE";
        case SYS_SEEK:           return "SYS_SEEK";
        case SYS_DEBUG_LOG:      return "SYS_DEBUG_LOG";
        case SYS_TCB_SET:        return "SYS_TCB_SET";
        case SYS_MEM_VM_MAP:     return "SYS_MEM_VM_MAP";
        case SYS_MEM_VM_UNMAP:   return "SYS_MEM_VM_UNMAP";
        case SYS_MEM_VM_PROTECT: return "SYS_MEM_VM_PROTECT";
        default:                 return "UNKNOWN_SYSCALL";
    }
}

const char* convert_syscall_ret(syscall_ret_t ret) {
    if(ret.is_error == false) { return "SUCCESS"; }
    switch(ret.err) {
        case ERROR_INVAL: return "ERROR_INVAL";
        case ERROR_NOMEM: return "ERROR_NOMEM";
        case ERROR_NOENT: return "ERROR_NOENT";
        case ERROR_ROFS:  return "ERROR_ROFS";
        case ERROR_BADFD: return "ERROR_BADFD";
        default:          return "UNKNOWN_SYSCALL_ERROR";
    }
}

syscall_ret_t syscall_sys_invalid(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void) arg1;
    (void) arg2;
    (void) arg3;
    (void) arg4;
    (void) arg5;
    (void) arg6;
    return SYSCALL_RET_ERROR(ERROR_INVAL);
}

// @note: syscall_nr is the LAST parameter so it's just *rsp
syscall_ret_t x86_64_dispatch_syscall(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6, syscall_nr_t syscall_nr) {
    x86_64_thread_t* thread = CPU_LOCAL_READ(current_thread);
#if SYSTRACE_ENABLED == 0
    (void) thread;
#endif

    if(syscall_nr >= MAX_SYSCALL_NUMBER) {
#if SYSTRACE_ENABLED == 1
        printf(
            "[systrace] %d:%d - (0x%llx) %s(0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx) = %s (0x%llx)\n",
            thread->common.process->pid,
            thread->common.tid,
            syscall_nr,
            convert_syscall_number(syscall_nr),
            arg1,
            arg2,
            arg3,
            arg4,
            arg5,
            arg6,
            convert_syscall_ret(SYSCALL_RET_ERROR(ERROR_INVAL)),
            ERROR_INVAL
        );
#endif
        return SYSCALL_RET_ERROR(ERROR_INVAL);
    }

    syscall_entry_t entry = syscall_table[syscall_nr];
    assert(entry.num_params <= 6 && "syscall entry has too many parameters");
#if SYSTRACE_ENABLED == 1
    char systrace_buf[1024];
    int index = snprintf(systrace_buf, 1024, "[systrace] %d:%d - (0x%llx) %s(", thread->common.process->pid, thread->common.tid, syscall_nr, convert_syscall_number(syscall_nr));
    switch(entry.num_params) {
        case 0:  index += snprintf(systrace_buf + index, 1024 - index, ")"); break;
        case 1:  index += snprintf(systrace_buf + index, 1024 - index, "0x%llx)", arg1); break;
        case 2:  index += snprintf(systrace_buf + index, 1024 - index, "0x%llx, 0x%llx)", arg1, arg2); break;
        case 3:  index += snprintf(systrace_buf + index, 1024 - index, "0x%llx, 0x%llx, 0x%llx)", arg1, arg2, arg3); break;
        case 4:  index += snprintf(systrace_buf + index, 1024 - index, "0x%llx, 0x%llx, 0x%llx, 0x%llx)", arg1, arg2, arg3, arg4); break;
        case 5:  index += snprintf(systrace_buf + index, 1024 - index, "0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx)", arg1, arg2, arg3, arg4, arg5); break;
        case 6:  index += snprintf(systrace_buf + index, 1024 - index, "0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx)", arg1, arg2, arg3, arg4, arg5, arg6); break;
        default: __builtin_unreachable();
    }

    printf("%s\n", systrace_buf);
#endif
    syscall_ret_t ret_value;

    switch(entry.num_params) {
        case 0:  ret_value = entry.handlers.handler0(); break;
        case 1:  ret_value = entry.handlers.handler1(arg1); break;
        case 2:  ret_value = entry.handlers.handler2(arg1, arg2); break;
        case 3:  ret_value = entry.handlers.handler3(arg1, arg2, arg3); break;
        case 4:  ret_value = entry.handlers.handler4(arg1, arg2, arg3, arg4); break;
        case 5:  ret_value = entry.handlers.handler5(arg1, arg2, arg3, arg4, arg5); break;
        case 6:  ret_value = entry.handlers.handler6(arg1, arg2, arg3, arg4, arg5, arg6); break;
        default: __builtin_unreachable();
    }

#if SYSTRACE_ENABLED == 1
    if(ret_value.is_error) {
        snprintf(systrace_buf + index, 1024 - index, " = %s (%lld)", convert_syscall_ret(ret_value), ret_value);
    } else {
        snprintf(systrace_buf + index, 1024 - index, " = SUCCESS (0x%llx)", ret_value.value);
    }
    printf("%s\n", systrace_buf);
#endif
    return ret_value;
}

#define SYSCALL_DISPATCHER(nr, __handler, __num_params)           \
    syscall_table[nr].num_params = __num_params;                  \
    syscall_table[nr].handlers.handler##__num_params = __handler;

void userspace_init() {
    uint64_t efer = read_msr(IA32_EFER);
    efer |= (1 << 0);
    write_msr(IA32_EFER, efer);

    uint64_t star = ((uint64_t) (0x18 | 3) << 48) | ((uint64_t) 0x08 << 32);
    write_msr(IA32_STAR, star);

    write_msr(IA32_LSTAR, (uint64_t) x86_64_handle_syscall);
    write_msr(IA32_SFMASK, ~0x2);

    for(size_t i = 0; i < MAX_SYSCALL_NUMBER; i++) {
        syscall_table[i].num_params = 6;
        syscall_table[i].handlers.handler6 = syscall_sys_invalid;
    }

    SYSCALL_DISPATCHER(SYS_EXIT, syscall_sys_exit, 1);

    SYSCALL_DISPATCHER(SYS_OPEN, syscall_sys_open, 4);
    SYSCALL_DISPATCHER(SYS_READ, syscall_sys_read, 3);
    SYSCALL_DISPATCHER(SYS_WRITE, syscall_sys_write, 3);
    SYSCALL_DISPATCHER(SYS_CLOSE, syscall_sys_close, 1);
    SYSCALL_DISPATCHER(SYS_SEEK, syscall_sys_seek, 3);

    SYSCALL_DISPATCHER(SYS_DEBUG_LOG, syscall_sys_debug_log, 2);

    SYSCALL_DISPATCHER(SYS_TCB_SET, syscall_sys_tcb_set, 1);

    SYSCALL_DISPATCHER(SYS_MEM_VM_MAP, syscall_sys_vm_map, 6);
    SYSCALL_DISPATCHER(SYS_MEM_VM_UNMAP, syscall_sys_vm_unmap, 2);
    SYSCALL_DISPATCHER(SYS_MEM_VM_PROTECT, syscall_sys_vm_protect, 3);
}
