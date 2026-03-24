#include <common/userspace/userspace.h>

syscall_ret_t syscall_sys_mem_anon_alloc(uint64_t size, uint64_t align);
syscall_ret_t syscall_sys_mem_anon_free(uint64_t addr);
