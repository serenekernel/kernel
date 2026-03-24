#include <common/userspace/userspace.h>
#include <memory/memory.h>
#include <stddef.h>

syscall_ret_t syscall_sys_write(uint64_t fd, virt_addr_t buf, size_t count);
