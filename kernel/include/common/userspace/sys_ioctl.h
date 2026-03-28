#pragma once

#include <common/userspace/userspace.h>
#include <memory/memory.h>
#include <stdint.h>

syscall_ret_t syscall_sys_ioctl(uint64_t req, virt_addr_t arg);
