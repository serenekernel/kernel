#include <common/arch.h>
#include <common/ldr/sysv.h>
#include <lib/buffer.h>
#include <memory/heap.h>
#include <memory/memory.h>
#include <string.h>

#include "memory/vmm.h"

typedef enum : uint64_t {
    AUXV_NULL = 0,
    AUXV_IGNORE = 1,
    AUXV_EXECFD = 2,
    AUXV_PHDR = 3,
    AUXV_PHENT = 4,
    AUXV_PHNUM = 5,
    AUXV_PAGESZ = 6,
    AUXV_BASE = 7,
    AUXV_FLAGS = 8,
    AUXV_ENTRY = 9,
    AUXV_NOTELF = 10,
    AUXV_UID = 11,
    AUXV_EUID = 12,
    AUXV_GID = 13,
    AUXV_EGID = 14,
    AUXV_SECURE = 23
} auxv_entry_t;

// @todo: should we move these?
void insert_u64(buffer_t* data, uint64_t value) {
    buffer_append(data, (uint8_t*) &value, sizeof(value));
}

void insert_auxv(buffer_t* data, auxv_entry_t entry, uint64_t value) {
    buffer_append(data, (uint8_t*) &entry, sizeof(entry));
    buffer_append(data, (uint8_t*) &value, sizeof(value));
}

virt_addr_t sysv_user_stack_init(process_t* process, virt_addr_t user_stack_top, elf64_elf_loader_info_t* loader_info) {
    char* argv[] = {};
    char* envp[] = {};
    size_t argc = sizeof(argv) / sizeof(argv[0]);
    size_t envc = sizeof(envp) / sizeof(envp[0]);

    uintptr_t* argv_p = heap_alloc(sizeof(uintptr_t) * argc);
    uintptr_t* envc_p = heap_alloc(sizeof(uintptr_t) * envc);

    size_t size_of_info_block = 0;
    for(size_t i = 0; i < argc; i++) { size_of_info_block += strlen(argv[i]) + 1; }
    for(size_t i = 0; i < envc; i++) { size_of_info_block += strlen(envp[i]) + 1; }

    uintptr_t arg_block = vmm_alloc_backed(process->allocator, ALIGN_UP(size_of_info_block, PAGE_SIZE_DEFAULT) / PAGE_SIZE_DEFAULT, VM_ACCESS_USER, VM_CACHE_NORMAL, VM_READ_WRITE, true);
    uintptr_t offset = 0;

    for(size_t i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]) + 1;
        vm_memcpy(process->allocator, &kernel_allocator, (virt_addr_t) (arg_block + offset), (virt_addr_t) argv[i], len);
        argv_p[i] = arg_block + offset;
        offset += len;
    }

    for(size_t i = 0; i < envc; i++) {
        size_t len = strlen(envp[i]) + 1;
        vm_memcpy(process->allocator, &kernel_allocator, (virt_addr_t) (arg_block + offset), (virt_addr_t) envp[i], len);
        envc_p[i] = arg_block + offset;
        offset += len;
    }

    buffer_t stack_buf = buffer_create(128);
    insert_u64(&stack_buf, argc); // argc
    for(size_t i = 0; i < argc; i++) { insert_u64(&stack_buf, argv_p[i]); }
    insert_u64(&stack_buf, 0); // argv null
    for(size_t i = 0; i < envc; i++) { insert_u64(&stack_buf, envc_p[i]); }
    insert_u64(&stack_buf, 0); // envp null

    insert_auxv(&stack_buf, AUXV_PHDR, loader_info->phdr_table);
    insert_auxv(&stack_buf, AUXV_PHENT, loader_info->phentsize);
    insert_auxv(&stack_buf, AUXV_PHNUM, loader_info->phnum);
    insert_auxv(&stack_buf, AUXV_PAGESZ, PAGE_SIZE_SMALL);
    if(loader_info->interp_base != 0) { insert_auxv(&stack_buf, AUXV_BASE, loader_info->interp_base); }
    insert_auxv(&stack_buf, AUXV_ENTRY, loader_info->image_entry_point);
    insert_u64(&stack_buf, AUXV_NULL);


    uintptr_t stack_pointer = ALIGN_DOWN(user_stack_top - (stack_buf.size), 16);
    printf("stack_pointer: %p\n", (void*) stack_pointer);
    vm_memcpy(process->allocator, &kernel_allocator, stack_pointer, (virt_addr_t) stack_buf.data, stack_buf.size);

    return stack_pointer;
}
