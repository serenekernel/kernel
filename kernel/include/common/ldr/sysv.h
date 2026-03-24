#pragma once
#include <common/ldr/elf.h>
#include <common/sched/process.h>
#include <memory/memory.h>
#include <stddef.h>
#include <stdint.h>

virt_addr_t sysv_user_stack_init(process_t* process, virt_addr_t stack_top, elf64_elf_loader_info_t* loader_info);
