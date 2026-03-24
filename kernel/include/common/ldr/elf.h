#pragma once
#include <common/ldr/elf_internal.h>
#include <common/sched/process.h>
#include <memory/memory.h>
#include <stdint.h>

typedef struct {
    virt_addr_t entry_point;
} elf64_elf_loader_info_t;

elf64_elf_loader_info_t* elf_load(process_t* process, const elf64_elf_header_t* header);
