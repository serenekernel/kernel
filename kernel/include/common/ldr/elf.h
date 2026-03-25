#pragma once
#include <common/ldr/elf_internal.h>
#include <common/sched/process.h>
#include <memory/memory.h>
#include <stdint.h>

typedef struct {
    virt_addr_t entry_point;
    virt_addr_t phdr_table;
    size_t phnum;
    size_t phentsize;
} elf64_elf_loader_info_t;

elf64_elf_loader_info_t* elf_load(process_t* process, const elf64_elf_header_t* elf_header);
bool elf_supported(const elf64_elf_header_t* elf_header);
