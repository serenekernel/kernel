#pragma once
#include <arch/memory.h>
#include <stdint.h>

#define IS_POWER_OF_TWO(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)
#define ALIGN_UP(x, align) ((((uintptr_t) (x)) + ((align) - 1)) & ~((uintptr_t) ((align) - 1)))
#define ALIGN_DOWN(x, align) (((uintptr_t) (x)) & ~((uintptr_t) ((align) - 1)))

typedef uintptr_t phys_addr_t;
typedef uintptr_t virt_addr_t;

typedef enum {
    PAGE_SIZE_DEFAULT = ARCH_PAGE_SIZE_DEFAULT,
    PAGE_SIZE_SMALL = ARCH_PAGE_SIZE_4K,
    PAGE_SIZE_LARGE = ARCH_PAGE_SIZE_2M,
    PAGE_SIZE_HUGE = ARCH_PAGE_SIZE_1G,
} page_size_t;

typedef struct {
    bool read    : 1;
    bool write   : 1;
    bool execute : 1;
} vm_protection_t;

#define VM_PROT_NO_ACCESS ((vm_protection_t) { .read = 0, .write = 0, .execute = 0 })
#define VM_PROT_RO ((vm_protection_t) { .read = 1, .write = 0, .execute = 0 })
#define VM_PROT_RW ((vm_protection_t) { .read = 1, .write = 1, .execute = 0 })
#define VM_PROT_RX ((vm_protection_t) { .read = 1, .write = 0, .execute = 1 })
#define VM_PROT_RWX ((vm_protection_t) { .read = 1, .write = 1, .execute = 1 })

typedef enum {
    VM_PRIVILEGE_KERNEL,
    VM_PRIVILEGE_USER,
} vm_privilege_t;

typedef enum {
    VM_CACHE_NORMAL,
    VM_CACHE_DISABLE,
    VM_CACHE_WRITE_THROUGH,
    VM_CACHE_WRITE_COMBINE
} vm_cache_t;

const char* limine_memmap_type_to_str(uint64_t type);

#define KERNELSPACE_START 0xFFFF'8000'0000'0000
#define KERNELSPACE_END ((uintptr_t) -PAGE_SIZE_DEFAULT)
#define USERSPACE_START (PAGE_SIZE_DEFAULT)
#define USERSPACE_END (((uintptr_t) 1 << 47) - PAGE_SIZE_DEFAULT)
