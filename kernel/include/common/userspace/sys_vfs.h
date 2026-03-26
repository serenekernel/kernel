#include <common/userspace/userspace.h>
#include <memory/memory.h>
#include <stddef.h>

syscall_ret_t syscall_sys_open(virt_addr_t pathname_str, size_t pathname_len, uint64_t flags, uint64_t mode);
syscall_ret_t syscall_sys_read(uint64_t fd, virt_addr_t buff, size_t count);
syscall_ret_t syscall_sys_write(uint64_t fd, virt_addr_t buf, size_t count);
syscall_ret_t syscall_sys_seek(uint64_t fd, size_t offset, size_t whence);
syscall_ret_t syscall_sys_close(uint64_t fd);
