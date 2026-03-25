#include <common/arch.h>
#include <common/ldr/sysv.h>
#include <memory/heap.h>
#include <memory/memory.h>
#include <string.h>

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


static virt_addr_t stackpush_8(virt_addr_t stack_top, uint64_t value) {
    stack_top -= sizeof(uint64_t);
    *(uint64_t*) stack_top = value;
    return stack_top;
}

static virt_addr_t stackpush_auxv(virt_addr_t stack_top, auxv_entry_t type, uint64_t value) {
    stack_top -= sizeof(uint64_t);
    *(uint64_t*) stack_top = value;
    stack_top -= sizeof(uint64_t);
    *(uint64_t*) stack_top = (uint64_t) type;
    return stack_top;
}

#define LOCAL_STACK_SIZE 512

virt_addr_t sysv_user_stack_init(process_t* process, virt_addr_t user_stack_top, elf64_elf_loader_info_t* loader_info) {
    uint8_t* buf = (uint8_t*) heap_alloc(LOCAL_STACK_SIZE);
    virt_addr_t buf_top = (virt_addr_t) buf + LOCAL_STACK_SIZE;

    virt_addr_t str_top = buf_top;

    virt_addr_t tbl = str_top;

    tbl = stackpush_8(tbl, AUXV_NULL);

    tbl = stackpush_auxv(tbl, AUXV_PAGESZ, PAGE_SIZE_DEFAULT);
    tbl = stackpush_auxv(tbl, AUXV_SECURE, 0);
    tbl = stackpush_auxv(tbl, AUXV_ENTRY, loader_info->entry_point);
    tbl = stackpush_auxv(tbl, AUXV_PHENT, loader_info->phentsize);
    tbl = stackpush_auxv(tbl, AUXV_PHDR, loader_info->phdr_table);
    tbl = stackpush_auxv(tbl, AUXV_PHNUM, loader_info->phnum);

    tbl = stackpush_8(tbl, 0); // envp end
    tbl = stackpush_8(tbl, 0); // argv end

    tbl = stackpush_8(tbl, 0); // argc

    size_t data_size = buf_top - tbl;
    virt_addr_t stack_pointer = ALIGN_DOWN(user_stack_top - data_size, 16);


    vm_memcpy(process->allocator, &kernel_allocator, stack_pointer, tbl, data_size);
    heap_free(buf, LOCAL_STACK_SIZE);
    return stack_pointer;
}
