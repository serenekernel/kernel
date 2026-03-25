#pragma once
#include <memory/memory.h>
#include <stdint.h>

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_elf_header_t;

#define ELF_CLASS_IDX 4
#define ELF_CLASS_64_BIT 2

#define ELF_DATA_IDX 5
#define ELF_DATA_2LSB 1

#define ETYPE_REL 1
#define ETYPE_EXEC 2
#define ETYPE_DYN 3

#define EMACHINE_X86_64 62

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_program_header_t;

#define PTYPE_LOAD 1
#define PTYPE_PHDR 6

#define PFLAGS_EXECUTE (1 << 0)
#define PFLAGS_WRITE (1 << 1)

typedef enum {
    KCREATE_PROC_NONE = 0,
    KCREATE_PROC_SUSPEND = 1
} kcreate_proc_flags;
