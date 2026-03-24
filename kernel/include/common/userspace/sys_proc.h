#include <common/userspace/userspace.h>

syscall_ret_t syscall_sys_exit(uint64_t exit_code);
syscall_ret_t syscall_sys_tcb_set(virt_addr_t pointer);
