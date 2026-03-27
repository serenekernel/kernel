#include <common/userspace/userspace.h>
#include <memory/memory.h>
#include <stddef.h>

syscall_ret_t syscall_sys_open(virt_addr_t pathname_str, size_t pathname_len, uint64_t flags, uint64_t mode);
syscall_ret_t syscall_sys_read(uint64_t fd, virt_addr_t buff, size_t count);
syscall_ret_t syscall_sys_write(uint64_t fd, virt_addr_t buf, size_t count);
syscall_ret_t syscall_sys_seek(uint64_t fd, size_t offset, size_t whence);
syscall_ret_t syscall_sys_close(uint64_t fd);
syscall_ret_t syscall_sys_is_a_tty(uint64_t fd);

syscall_ret_t syscall_sys_stat(uint64_t fd, uint64_t statbuf);
syscall_ret_t syscall_sys_stat_at(uint64_t fd, uint64_t path, size_t path_len, uint64_t statbuf, size_t flag);
syscall_ret_t syscall_sys_getcwd(virt_addr_t buf, size_t size);
