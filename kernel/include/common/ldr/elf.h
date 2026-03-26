#pragma once
#include <common/ldr/elf_internal.h>
#include <common/sched/process.h>
#include <memory/memory.h>
#include <stdint.h>

#include "common/fs/vfs.h"

typedef struct {
    // @note: address of where we should start execution
    virt_addr_t executable_entry_point;
    // @note: address of the entry point in the image
    virt_addr_t image_entry_point;

    virt_addr_t phdr_table;
    size_t phnum;
    size_t phentsize;
    // @note: offset of the image in memory from it's lowest address (for DYN images)
    virt_addr_t image_offset;

    // @note: load base of the interpreter (ld.so); 0 if no interpreter (AT_BASE)
    virt_addr_t interp_base;
} elf64_elf_loader_info_t;

bool elf_load_file(process_t* process, vfs_path_t* path, elf64_elf_loader_info_t** loader_info);

bool elf_supported(const elf64_elf_header_t* elf_header);
