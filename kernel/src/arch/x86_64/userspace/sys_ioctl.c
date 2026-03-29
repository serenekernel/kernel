#include <arch/cpu_local.h>
#include <common/tty.h>
#include <common/userspace/sys_ioctl.h>
#include <memory/memory.h>
#include <memory/vm.h>

#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCGWINSZ 0x5413

typedef struct {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} winsize_t;

syscall_ret_t syscall_sys_ioctl(uint64_t req, virt_addr_t arg) {
    LOG_INFO("sys_ioctl: req: 0x%llx, arg: 0x%llx\n", req, arg);
    process_t* proc = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    // @todo: fg process tty
    switch(req) {
        case TIOCGPGRP: {
            if(!validate_user_buffer(proc, arg, sizeof(int64_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }
            int64_t pgid = g_tty->owner_process_group_id;
            vm_copy_to(proc->address_space, (virt_addr_t) arg, &pgid, sizeof(int64_t));
            return SYSCALL_RET_VALUE(0);
        }
        case TIOCSPGRP: {
            if(!validate_user_buffer(proc, arg, sizeof(int64_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }
            int64_t pgid;
            vm_copy_from(&pgid, proc->address_space, (virt_addr_t) arg, sizeof(int64_t));
            g_tty->owner_process_group_id = pgid;
            return SYSCALL_RET_VALUE(0);
        }
        case TIOCGWINSZ: {
            winsize_t ws;
            if(!validate_user_buffer(proc, arg, sizeof(winsize_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }
            ws.ws_col = 80;
            ws.ws_row = 25;
            vm_copy_to(proc->address_space, (virt_addr_t) arg, &ws, sizeof(winsize_t));
            return SYSCALL_RET_VALUE(0);
        }
        default: return SYSCALL_RET_ERROR(ERROR_INVAL);
    }
}
