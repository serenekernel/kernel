#include <common/userspace/userspace.h>

syscall_ret_t syscall_sys_vm_map(virt_addr_t hint, size_t size, size_t prot, size_t flags, size_t fd, size_t offset);
syscall_ret_t syscall_sys_vm_unmap(uint64_t addr, size_t size);
syscall_ret_t syscall_sys_vm_protect(uintptr_t pointer, size_t size, size_t prot);
