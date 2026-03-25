#include <common/ldr/sysv.h>

#include "common/arch.h"
#include "memory/memory.h"

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

virt_addr_t stackpush_8(virt_addr_t stack_top, uint64_t value) {
    *(virt_addr_t*) stack_top = value;
    stack_top -= sizeof(uint64_t);

    return stack_top;
}

virt_addr_t stackpush_auxv(virt_addr_t stack_top, auxv_entry_t entry, uint64_t value) {
    *(virt_addr_t*) stack_top = value;
    stack_top -= sizeof(uint64_t);

    *(virt_addr_t*) stack_top = entry;
    stack_top -= sizeof(uint64_t);
    return stack_top;
}

#define LOCAL_STACK_SIZE 512

size_t write_stack_data(virt_addr_t local_stack_top, elf64_elf_loader_info_t* loader_info) {
    virt_addr_t stack = (virt_addr_t) local_stack_top;
    stack = stackpush_8(stack, AUXV_NULL);
    stack = stackpush_auxv(stack, AUXV_PHNUM, loader_info->phnum);
    stack = stackpush_auxv(stack, AUXV_PHDR, loader_info->phdr_table);
    stack = stackpush_auxv(stack, AUXV_PHENT, loader_info->phentsize);
    stack = stackpush_auxv(stack, AUXV_ENTRY, loader_info->entry_point);
    stack = stackpush_auxv(stack, AUXV_SECURE, 0);
    stack = stackpush_auxv(stack, AUXV_PAGESZ, PAGE_SIZE_DEFAULT);

    stack = stackpush_8(stack, 0); // envp null term
    stack = stackpush_8(stack, 0); // argv null term
    stack = stackpush_8(stack, 0); // argc

    return local_stack_top - stack;
}

virt_addr_t sysv_user_stack_init(process_t* process, virt_addr_t user_stack_top, elf64_elf_loader_info_t* loader_info) {
    // @todo: refactor this
    uint8_t stack_data[LOCAL_STACK_SIZE] = {};
    size_t size_of_stack_data = write_stack_data((virt_addr_t) stack_data + LOCAL_STACK_SIZE, loader_info);
    virt_addr_t stack_pointer = ALIGN_DOWN(user_stack_top - size_of_stack_data, 16);

    vm_memcpy(process->allocator, &kernel_allocator, stack_pointer, (virt_addr_t) stack_data, size_of_stack_data);
    return stack_pointer;
}
