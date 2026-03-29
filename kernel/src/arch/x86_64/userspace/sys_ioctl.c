#include <arch/cpu_local.h>
#include <common/tty.h>
#include <common/userspace/sys_ioctl.h>
#include <memory/memory.h>
#include <memory/vm.h>

#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCGWINSZ 0x5413

#define TCGETS 0x5401
#define TCSETS 0x5402

typedef struct {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} winsize_t;

syscall_ret_t ioctl_tiocgpgrp(process_t* process, virt_addr_t in_out_arg) {
    if(!validate_user_buffer(process, in_out_arg, sizeof(int64_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }
    int64_t pgid = g_tty->owner_process_group_id;
    vm_copy_to(process->address_space, (virt_addr_t) in_out_arg, &pgid, sizeof(int64_t));
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t ioctl_tiocspgrp(process_t* process, virt_addr_t in_out_arg) {
    if(!validate_user_buffer(process, in_out_arg, sizeof(int64_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }
    int64_t pgid;
    vm_copy_from(&pgid, process->address_space, (virt_addr_t) in_out_arg, sizeof(int64_t));
    g_tty->owner_process_group_id = pgid;
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t ioctl_tiocgwinsz(process_t* process, virt_addr_t in_out_arg) {
    winsize_t ws;
    if(!validate_user_buffer(process, in_out_arg, sizeof(winsize_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }
    ws.ws_col = 80;
    ws.ws_row = 25;
    vm_copy_to(process->address_space, (virt_addr_t) in_out_arg, &ws, sizeof(winsize_t));
    return SYSCALL_RET_VALUE(0);
}

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;
#define NCCS 32

typedef struct {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
    speed_t c_ibaud;
    speed_t c_obaud;
} termios_t;

#define ISIG 0000001
#define ICANON 0000002
#define ECHO 0000010
#define ECHOE 0000020
#define ECHOK 0000040

syscall_ret_t ioctl_tcgets(process_t* process, virt_addr_t in_out_arg) {
    termios_t termios = {};
    if(!validate_user_buffer(process, in_out_arg, sizeof(termios_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }
    termios.c_lflag = ICANON | ECHO | ECHOE | ECHOK;
    vm_copy_to(process->address_space, (virt_addr_t) in_out_arg, &termios, sizeof(termios_t));
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t ioctl_tcsets(process_t* process, virt_addr_t in_out_arg) {
    (void) process;
    (void) in_out_arg;
    termios_t termios = {};
    if(!validate_user_buffer(process, in_out_arg, sizeof(termios_t))) { return SYSCALL_RET_ERROR(ERROR_FAULT); }

    vm_copy_from(&termios, process->address_space, (virt_addr_t) in_out_arg, sizeof(termios_t));
    LOG_INFO("ioctl_tcsets: c_iflag: 0x%08x, c_oflag: 0x%08x, c_cflag: 0x%08x, c_lflag: 0x%08x\n", termios.c_iflag, termios.c_oflag, termios.c_cflag, termios.c_lflag);

    // assert(false);
    return SYSCALL_RET_VALUE(0);
}


const char* ioctl_request_to_string(uint64_t req) {
    switch(req) {
        case TIOCGPGRP:  return "TIOCGPGRP";
        case TIOCSPGRP:  return "TIOCSPGRP";
        case TIOCGWINSZ: return "TIOCGWINSZ";
        case TCGETS:     return "TCGETS";
        case TCSETS:     return "TCSETS";
        default:         return "UNKNOWN_IOCTL_REQUEST";
    }
}

syscall_ret_t syscall_sys_ioctl(uint64_t req, virt_addr_t in_out_arg) {
    LOG_INFO("sys_ioctl: req: %s (0x%llx), arg: 0x%llx\n", ioctl_request_to_string(req), req, in_out_arg);
    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    // @todo: fg process tty
    switch(req) {
        case TIOCGPGRP:  return ioctl_tiocgpgrp(process, in_out_arg);
        case TIOCSPGRP:  return ioctl_tiocspgrp(process, in_out_arg);
        case TIOCGWINSZ: return ioctl_tiocgwinsz(process, in_out_arg);
        case TCGETS:     return ioctl_tcgets(process, in_out_arg);
        case TCSETS:     return ioctl_tcsets(process, in_out_arg);
        default:         return SYSCALL_RET_ERROR(ERROR_INVAL);
    }
}
