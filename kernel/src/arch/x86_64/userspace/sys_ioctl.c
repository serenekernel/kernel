#include <arch/cpu_local.h>
#include <common/tty.h>
#include <common/userspace/sys_ioctl.h>

#include "memory/memory.h"
#include "memory/vmm.h"


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
    printf("[sys_ioctl] req: 0x%llx, arg: 0x%llx\n", req, arg);
    process_t* proc = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    // @todo: fg process tty
    switch(req) {
        case TIOCGPGRP: {
            if(!validate_user_buffer(proc, arg, sizeof(int64_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }
            vm_memcpy(proc->allocator, &kernel_allocator, (virt_addr_t) arg, (virt_addr_t) &g_tty->owner_process_group_id, sizeof(int64_t));
            return SYSCALL_RET_VALUE(0);
        }
        case TIOCSPGRP: {
            if(!validate_user_buffer(proc, arg, sizeof(int64_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }
            vm_memcpy(&kernel_allocator, proc->allocator, (virt_addr_t) &g_tty->owner_process_group_id, (virt_addr_t) arg, sizeof(int64_t));
            return SYSCALL_RET_VALUE(0);
        }
        case TIOCGWINSZ: {
            winsize_t ws;
            if(!validate_user_buffer(proc, arg, sizeof(winsize_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }
            ws.ws_col = 80;
            ws.ws_row = 25;
            vm_memcpy(proc->allocator, &kernel_allocator, (virt_addr_t) arg, (virt_addr_t) &ws, sizeof(winsize_t));
            return SYSCALL_RET_VALUE(0);
        }
        default: return SYSCALL_RET_ERROR(ERROR_INVAL);
    }
}
