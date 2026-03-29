// slightly plagiarized from
// https://github.com/elysium-os/cronus/blob/ea9b48c99e29781aa5f6f30fb887c7a02b68e30e/arch/x86_64/ptm.c
#include <arch/hardware/lapic.h>
#include <arch/internal/cpuid.h>
#include <arch/internal/cr.h>
#include <arch/memory.h>
#include <arch/msr.h>
#include <assert.h>
#include <common/arch.h>
#include <common/cpu_local.h>
#include <common/ipi.h>
#include <common/requests.h>
#include <lib/spinlock.h>
#include <memory/heap.h>
#include <memory/memory.h>
#include <memory/pagedb.h>
#include <memory/pmm.h>
#include <memory/vm.h>
#include <rbtree.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define VADDR_TO_INDEX(VADDR, LEVEL) (((VADDR) >> ((LEVEL) * 9 + 3)) & 0x1FF)
#define INDEX_TO_VADDR(INDEX, LEVEL) ((uint64_t) (INDEX) << ((LEVEL) * 9 + 3))
#define LEVEL_TO_PAGESIZE(LEVEL) (1UL << (12 + 9 * ((LEVEL) - 1)))


#define PAGE_BIT_PRESENT (1 << 0)
#define PAGE_BIT_RW (1 << 1)
#define PAGE_BIT_USER (1 << 2)
#define PAGE_BIT_WRITETHROUGH (1 << 3)
#define PAGE_BIT_DISABLECACHE (1 << 4)
#define PAGE_BIT_ACCESSED (1 << 5)
#define PAGE_BIT_GLOBAL (1 << 8)
#define PAGE_BIT_NX ((uint64_t) 1 << 63)
#define PAGE_BIT_PAT(PAGE_SIZE) ((PAGE_SIZE) == ARCH_PAGE_SIZE_4K ? SMALL_PAGE_BIT_PAT : HUGE_PAGE_BIT_PAT)
#define PAGE_ADDRESS_MASK(PAGE_SIZE) ((PAGE_SIZE) == ARCH_PAGE_SIZE_4K ? SMALL_PAGE_ADDRESS_MASK : HUGE_PAGE_ADDRESS_MASK)

#define SMALL_PAGE_BIT_PAT (1 << 7)
#define SMALL_PAGE_ADDRESS_MASK ((uint64_t) 0x000F'FFFF'FFFF'F000)

#define HUGE_PAGE_BIT_PAGE_STOP (1 << 7)
#define HUGE_PAGE_BIT_PAT (1 << 12)
#define HUGE_PAGE_ADDRESS_MASK ((uint64_t) 0x000F'FFFF'FFFF'0000)

#define PAT0 (0)
#define PAT1 (PAGE_BIT_WRITETHROUGH)
#define PAT2 (PAGE_BIT_DISABLECACHE)
#define PAT3 (PAGE_BIT_DISABLECACHE | PAGE_BIT_WRITETHROUGH)
#define PAT4(PAT_FLAG) (PAT_FLAG)
#define PAT5(PAT_FLAG) ((PAT_FLAG) | PAGE_BIT_WRITETHROUGH)
#define PAT6(PAT_FLAG) ((PAT_FLAG) | PAGE_BIT_DISABLECACHE)
#define PAT7(PAT_FLAG) ((PAT_FLAG) | PAGE_BIT_DISABLECACHE | PAGE_BIT_WRITETHROUGH)

extern vm_address_space_t* g_global_address_space;
static vm_region_t g_kernel_region;

static bool g_pat_supported = false;
static bool g_1gb_page_supported = false;
static bool g_using_5_level_paging = false;
#define LEVEL_COUNT (g_using_5_level_paging ? 5 : 4)

static uintptr_t alloc_page() {
    return pmm_alloc_page(PMM_FLAG_ZERO);
}

void ptm_flush_page(virt_addr_t vaddr, size_t length) {
    for(; length > 0; length -= PAGE_SIZE_DEFAULT, vaddr += PAGE_SIZE_DEFAULT) asm volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

static uint64_t privilege_to_x86_flags(vm_privilege_t privilege) {
    switch(privilege) {
        case VM_PRIVILEGE_KERNEL: return 0;
        case VM_PRIVILEGE_USER:   return PAGE_BIT_USER;
    }
    __builtin_unreachable();
}

static uint64_t cache_to_x86_flags(vm_cache_t cache, page_size_t page_size) {
    switch(cache) {
        case VM_CACHE_NORMAL:        return PAT0;
        case VM_CACHE_WRITE_COMBINE: return PAT6(PAGE_BIT_PAT(page_size));
        case VM_CACHE_DISABLE:       return PAT3;
        case VM_CACHE_WRITE_THROUGH: return 0;
    }
    __builtin_unreachable();
}

static uint64_t break_big(uint64_t* table, int index, int current_level) {
    assert(current_level > 0);

    uint64_t entry = table[index];

    uintptr_t address = entry & HUGE_PAGE_ADDRESS_MASK;
    bool pat = entry & HUGE_PAGE_BIT_PAT;
    entry &= ~SMALL_PAGE_ADDRESS_MASK;

    uint64_t new_entry = entry;
    if(current_level - 1 == 0) {
        new_entry &= ~SMALL_PAGE_BIT_PAT;
        if(pat) new_entry |= SMALL_PAGE_BIT_PAT;
    } else {
        new_entry |= HUGE_PAGE_BIT_PAT;
    }

    entry |= alloc_page();
    if(pat) entry |= SMALL_PAGE_BIT_PAT;

    uint64_t* new_table = (void*) (entry & SMALL_PAGE_ADDRESS_MASK);
    for(int i = 0; i < 512 * ARCH_PAGE_SIZE_4K; i += ARCH_PAGE_SIZE_4K) new_table[i] = new_entry | (address + i);

    __atomic_store(&table[index], &entry, __ATOMIC_SEQ_CST);

    return entry;
}

static void map_page(uint64_t* pml4, uintptr_t vaddr, uintptr_t paddr, page_size_t page_size, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global) {
    int lowest_index;
    switch(page_size) {
        case ARCH_PAGE_SIZE_4K: lowest_index = 1; break;
        case ARCH_PAGE_SIZE_2M: lowest_index = 2; break;
        case ARCH_PAGE_SIZE_1G: lowest_index = 3; break;
    }

    uint64_t* current_table = pml4;
    for(int j = LEVEL_COUNT; j > lowest_index; j--) {
        int index = VADDR_TO_INDEX(vaddr, j);

        uint64_t entry = current_table[index];
        if((entry & PAGE_BIT_PRESENT) == 0) {
            entry = PAGE_BIT_PRESENT | (alloc_page() & SMALL_PAGE_ADDRESS_MASK);
            if(!prot.execute) entry |= PAGE_BIT_NX;
        } else {
            if((entry & HUGE_PAGE_BIT_PAGE_STOP) != 0) entry = break_big(current_table, index, j);
            if(prot.execute) entry &= ~PAGE_BIT_NX;
        }
        if(prot.write) entry |= PAGE_BIT_RW;
        entry |= privilege_to_x86_flags(privilege);
        __atomic_store(&current_table[index], &entry, __ATOMIC_SEQ_CST);

        current_table = (uint64_t*) TO_HHDM(entry & SMALL_PAGE_ADDRESS_MASK);
    }

    uint64_t entry = PAGE_BIT_PRESENT | (paddr & PAGE_ADDRESS_MASK(page_size)) | privilege_to_x86_flags(privilege) | cache_to_x86_flags(cache, page_size);
    if(page_size != ARCH_PAGE_SIZE_4K) entry |= HUGE_PAGE_BIT_PAGE_STOP;
    if(prot.write) entry |= PAGE_BIT_RW;
    if(!prot.execute) entry |= PAGE_BIT_NX;
    if(global) entry |= PAGE_BIT_GLOBAL;
    __atomic_store(&current_table[VADDR_TO_INDEX(vaddr, lowest_index)], &entry, __ATOMIC_SEQ_CST);
}

void ptm_map(vm_address_space_t* address_space, uintptr_t vaddr, uintptr_t paddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global) {
    // LOG_INFO("map(as: %lx-%lx, vaddr: %lx, paddr: %lx, length: %lx, prot: %c%c%c)\n", address_space->start, address_space->end, vaddr, paddr, length, prot.read ? 'R' : '-', prot.write ? 'W' : '-', prot.execute ? 'X' : '-');

    assert(vaddr % ARCH_PAGE_SIZE_4K == 0);
    assert(paddr % ARCH_PAGE_SIZE_4K == 0);
    assert(length % ARCH_PAGE_SIZE_4K == 0);

    if(!prot.read) LOG_FAIL("No-read mapping is not supported on x86_64");
    spinlock_lock(&address_space->ptm.ptm_lock);

    for(size_t i = 0; i < length;) {
        page_size_t cursize = ARCH_PAGE_SIZE_4K;
        if(paddr % ARCH_PAGE_SIZE_2M == 0 && vaddr % ARCH_PAGE_SIZE_2M == 0 && length - i >= ARCH_PAGE_SIZE_2M) cursize = ARCH_PAGE_SIZE_2M;

        if(g_1gb_page_supported && paddr % ARCH_PAGE_SIZE_1G == 0 && vaddr % ARCH_PAGE_SIZE_1G == 0 && length - i >= ARCH_PAGE_SIZE_1G) cursize = ARCH_PAGE_SIZE_1G;

        map_page((uint64_t*) TO_HHDM(address_space->ptm.ptm_root), vaddr + i, paddr + i, cursize, prot, cache, privilege, global);

        i += cursize;
    }

    ptm_flush_page(vaddr, length);
    ipi_broadcast_flush_tlb(vaddr, length);
    spinlock_unlock(&address_space->ptm.ptm_lock);
}

void ptm_rewrite(vm_address_space_t* address_space, uintptr_t vaddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global) {
    // LOG_INFO("rewrite(as: %lx-%lx, vaddr: %lx, length: %lx, prot: %c%c%c)\n", address_space->start, address_space->end, vaddr, length, prot.read ? 'R' : '-', prot.write ? 'W' : '-', prot.execute ? 'X' : '-');

    assert(vaddr % ARCH_PAGE_SIZE_4K == 0);
    assert(length % ARCH_PAGE_SIZE_4K == 0);

    if(!prot.read) LOG_FAIL("No-read mapping is not supported on x86_64");
    spinlock_lock(&address_space->ptm.ptm_lock);

    for(size_t i = 0; i < length;) {
        uint64_t* current_table = (uint64_t*) TO_HHDM(address_space->ptm.ptm_root);

        int j = LEVEL_COUNT;
        for(; j > 1; j--) {
            int index = VADDR_TO_INDEX(vaddr + i, j);
            uint64_t entry = current_table[index];

            if((entry & PAGE_BIT_PRESENT) == 0) goto skip;
            if((entry & HUGE_PAGE_BIT_PAGE_STOP) != 0) {
                assert(j <= 3);
                if(INDEX_TO_VADDR(index, j) < vaddr + i || LEVEL_TO_PAGESIZE(j) > length - i) {
                    entry = break_big(current_table, index, j);
                } else {
                    break;
                }
            }

            if(prot.write) entry |= PAGE_BIT_RW;
            if(prot.execute) entry &= ~PAGE_BIT_NX;
            __atomic_store_n(&current_table[index], entry, __ATOMIC_SEQ_CST);

            current_table = (uint64_t*) TO_HHDM(current_table[index] & SMALL_PAGE_ADDRESS_MASK);
        }

        int index = VADDR_TO_INDEX(vaddr + i, j);
        uint64_t entry = current_table[index] | privilege_to_x86_flags(privilege) | cache_to_x86_flags(cache, j == 0 ? ARCH_PAGE_SIZE_4K : (j == 1 ? ARCH_PAGE_SIZE_2M : ARCH_PAGE_SIZE_1G));

        if(prot.write)
            entry |= PAGE_BIT_RW;
        else
            entry &= ~PAGE_BIT_RW;

        if(!prot.execute)
            entry |= PAGE_BIT_NX;
        else
            entry &= ~PAGE_BIT_NX;

        if(global)
            entry |= PAGE_BIT_GLOBAL;
        else
            entry &= ~PAGE_BIT_GLOBAL;

        __atomic_store_n(&current_table[index], entry, __ATOMIC_SEQ_CST);

    skip:
        i += LEVEL_TO_PAGESIZE(j);
    }

    ptm_flush_page(vaddr, length);
    ipi_broadcast_flush_tlb(vaddr, length);
    spinlock_unlock(&address_space->ptm.ptm_lock);
}

void ptm_unmap(vm_address_space_t* address_space, uintptr_t vaddr, size_t length) {
    // LOG_INFO("unmap(as: %lx-%lx, vaddr: %lx, length: %lx)\n", address_space->start, address_space->end, vaddr, length);

    assert(vaddr % ARCH_PAGE_SIZE_4K == 0);
    assert(length % ARCH_PAGE_SIZE_4K == 0);

    spinlock_lock(&address_space->ptm.ptm_lock);

    for(size_t i = 0; i < length;) {
        uint64_t* current_table = (uint64_t*) TO_HHDM(address_space->ptm.ptm_root);

        int j = LEVEL_COUNT;
        for(; j > 1; j--) {
            int index = VADDR_TO_INDEX(vaddr + i, j);
            uint64_t entry = current_table[index];

            if((entry & PAGE_BIT_PRESENT) == 0) goto skip;
            if((entry & HUGE_PAGE_BIT_PAGE_STOP) != 0) {
                assert(j <= 3);
                if(INDEX_TO_VADDR(index, j) < vaddr + i || LEVEL_TO_PAGESIZE(j) > length - i) {
                    entry = break_big(current_table, index, j);
                } else {
                    break;
                }
            }
            current_table = (uint64_t*) TO_HHDM(entry & SMALL_PAGE_ADDRESS_MASK);
        }
        __atomic_store_n(&current_table[VADDR_TO_INDEX(vaddr + i, j)], 0, __ATOMIC_SEQ_CST);

    skip:
        i += LEVEL_TO_PAGESIZE(j);
    }

    ptm_flush_page(vaddr, length);
    ipi_broadcast_flush_tlb(vaddr, length);
    spinlock_unlock(&address_space->ptm.ptm_lock);
}

bool ptm_physical(vm_address_space_t* address_space, uintptr_t vaddr, uintptr_t* paddr) {
    spinlock_lock(&address_space->ptm.ptm_lock);

    uint64_t* current_table = (uint64_t*) TO_HHDM(address_space->ptm.ptm_root);
    int j = LEVEL_COUNT;
    for(; j > 1; j--) {
        int index = VADDR_TO_INDEX(vaddr, j);
        if((current_table[index] & PAGE_BIT_PRESENT) == 0) {
            spinlock_unlock(&address_space->ptm.ptm_lock);
            return false;
        }
        if((current_table[index] & HUGE_PAGE_BIT_PAGE_STOP) != 0) break;
        current_table = (uint64_t*) TO_HHDM(current_table[index] & SMALL_PAGE_ADDRESS_MASK);
    }

    uint64_t entry = current_table[VADDR_TO_INDEX(vaddr, j)];
    spinlock_unlock(&address_space->ptm.ptm_lock);
    if((entry & PAGE_BIT_PRESENT) == 0) return false;

    switch(j) {
        case 1:  *paddr = ((entry & SMALL_PAGE_ADDRESS_MASK) + (vaddr & (~0x000F'FFFF'FFFF'F000))); break;
        case 2:  *paddr = ((entry & HUGE_PAGE_ADDRESS_MASK) + (vaddr & (~0x000F'FFFF'FFE0'0000))); break;
        case 3:  *paddr = ((entry & HUGE_PAGE_ADDRESS_MASK) + (vaddr & (~0x000F'FFFF'C000'0000))); break;
        default: __builtin_unreachable();
    }
    return true;
}


void ptm_load_address_space(vm_address_space_t* address_space) {
    __write_cr3(address_space->ptm.ptm_root);
}

#define PAT_UNCACHEABLE 0
#define PAT_WRITE_COMBINING 1
#define PAT_WRITE_THROUGH 4
#define PAT_WRITE_PROTECT 5
#define PAT_WRITE_BACK 6
#define PAT_UNCACHED 7
void setup_pat() {
    uint8_t pat0 = PAT_WRITE_BACK;
    uint8_t pat1 = PAT_WRITE_THROUGH;
    uint8_t pat2 = PAT_UNCACHED;
    uint8_t pat3 = PAT_UNCACHEABLE;
    uint8_t pat4 = PAT_WRITE_COMBINING;
    uint8_t pat5 = PAT_UNCACHEABLE; // UNUSED
    uint8_t pat6 = PAT_UNCACHEABLE; // UNUSED
    uint8_t pat7 = PAT_UNCACHEABLE; // UNUSED
    uint64_t pat = pat0 | ((uint64_t) pat1 << 8) | ((uint64_t) pat2 << 16) | ((uint64_t) pat3 << 24) | ((uint64_t) pat4 << 32) | ((uint64_t) pat5 << 40) | ((uint64_t) pat6 << 48) | ((uint64_t) pat7 << 56);
    write_msr(IA32_PAT_MSR, pat);
}


#define MAP_SEGMENT(name, map_type)                                                                                                                                                                                          \
    {                                                                                                                                                                                                                        \
        extern char name##_start[];                                                                                                                                                                                          \
        extern char name##_end[];                                                                                                                                                                                            \
        uintptr_t offset = name##_start - kernel_start;                                                                                                                                                                      \
        uintptr_t size = name##_end - name##_start;                                                                                                                                                                          \
        LOG_INFO("%s - 0x%llx, 0x%llx\n", #name, offset, size);                                                                                                                                                              \
        for(uintptr_t i = offset; i < offset + size; i += PAGE_SIZE_DEFAULT) { ptm_map(g_global_address_space, kernel_virt + i, kernel_phys + i, PAGE_SIZE_DEFAULT, map_type, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true); } \
    }

void map_kernel() {
    extern char kernel_start[];
    extern char kernel_end[];
    phys_addr_t kernel_phys = (phys_addr_t) kernel_mapping.response->physical_base;
    virt_addr_t kernel_virt = (virt_addr_t) kernel_start;
    virt_addr_t kernel_size = ((virt_addr_t) kernel_end) - ((virt_addr_t) kernel_start);

    MAP_SEGMENT(text, VM_PROT_RX);
    MAP_SEGMENT(rodata, VM_PROT_RO);
    MAP_SEGMENT(data, VM_PROT_RW);
    MAP_SEGMENT(requests, VM_PROT_RW);

    g_kernel_region.address_space = g_global_address_space;
    g_kernel_region.base = kernel_virt;
    g_kernel_region.length = kernel_size;
    g_kernel_region.protection = VM_PROT_RX;
    g_kernel_region.cache = VM_CACHE_NORMAL;
    g_kernel_region.dynamically_backed = false;
    g_kernel_region.type = VM_REGION_TYPE_DIRECT;
    g_kernel_region.type_data.direct.physical_address = 0;

    rb_insert(&g_global_address_space->regions_tree, &g_kernel_region.region_tree_node);

    for(size_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry* current = memmap_request.response->entries[i];

        const phys_addr_t aligned_paddr = ALIGN_DOWN(current->base, PAGE_SIZE_DEFAULT);
        const virt_addr_t vaddr = TO_HHDM(aligned_paddr);
        const virt_addr_t alignment_diff = current->base - aligned_paddr;
        const size_t aligned_length = ALIGN_UP(current->length + alignment_diff, PAGE_SIZE_DEFAULT);

        LOG_INFO("0x%016llx -> 0x%016llx (0x%08llx | %s)", aligned_paddr, vaddr, current->length, limine_memmap_type_to_str(current->type));
        if(current->type == LIMINE_MEMMAP_USABLE || current->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE || current->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) {
            printf(" will be mapped\n");
        } else {
            printf(" will not be mapped\n");
        }

        if(current->type == LIMINE_MEMMAP_USABLE || current->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE || current->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) {
            ptm_map(g_global_address_space, vaddr, aligned_paddr, aligned_length, VM_PROT_RW, VM_CACHE_NORMAL, VM_PRIVILEGE_KERNEL, true);
        }

        /*
         *  else if(current->type == LIMINE_MEMMAP_FRAMEBUFFER) {
             vm_map_pages_continuous(&kernel_allocator, virt_base, phys_base, page_count, VM_ACCESS_KERNEL, VM_CACHE_WRITE_COMBINE, VM_READ_WRITE);
         } else if(current->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE || current->type == LIMINE_MEMMAP_RESERVED_MAPPED || current->type == LIMINE_MEMMAP_ACPI_NVS) {
             vm_map_pages_continuous(&kernel_allocator, virt_base, phys_base, page_count, VM_ACCESS_KERNEL, VM_CACHE_NORMAL, VM_READ_ONLY);
         */
    }
}

void ptm_init_kernel_bsp() {
    g_global_address_space = (void*) TO_HHDM(pmm_alloc_page(PMM_FLAG_ZERO));
    g_global_address_space->ptm.ptm_root = alloc_page();
    g_global_address_space->ptm.ptm_lock = SPINLOCK_INIT;
    g_global_address_space->lock = SPINLOCK_INIT;
    g_global_address_space->regions_tree = vm_create_regions();
    g_global_address_space->start = KERNELSPACE_START;
    g_global_address_space->end = KERNELSPACE_END;

    g_pat_supported = __cpuid_is_feature_supported(CPUID_FEATURE_PAT);
    g_1gb_page_supported = __cpuid_is_feature_supported(CPUID_FEATURE_PDPE1GB_PAGES);
    g_using_5_level_paging = __read_cr4() & (1 << 12); // cr4.LA57

    if(g_pat_supported) { setup_pat(); }

    uint64_t* pml4 = (uint64_t*) TO_HHDM(g_global_address_space->ptm.ptm_root);
    for(int i = 256; i < 512; i++) { pml4[i] = PAGE_BIT_PRESENT | PAGE_BIT_RW | (alloc_page() & SMALL_PAGE_ADDRESS_MASK); }

    map_kernel();
    ptm_load_address_space(g_global_address_space);
    printf("ptm\n");
    vm_map_direct(g_global_address_space, (void*) lapic_get_base_address(), PAGE_SIZE_DEFAULT, VM_PROT_RW, VM_CACHE_DISABLE, FROM_HHDM(lapic_get_base_address()), VM_FLAG_FIXED);
}

void ptm_init_kernel_ap() {
    if(g_pat_supported) { setup_pat(); }
    ptm_load_address_space(g_global_address_space);
}

void ptm_init_user(vm_address_space_t* address_space) {
    address_space->ptm.ptm_root = alloc_page();
    address_space->ptm.ptm_lock = SPINLOCK_INIT;
    address_space->is_user = true;
    address_space->lock = SPINLOCK_INIT;
    address_space->regions_tree = vm_create_regions();
    address_space->start = USERSPACE_START;
    address_space->end = USERSPACE_END;

    memcpy((void*) TO_HHDM(address_space->ptm.ptm_root + (256 * sizeof(uint64_t))), (void*) TO_HHDM(g_global_address_space->ptm.ptm_root + (256 * sizeof(uint64_t))), 256 * sizeof(uint64_t));
    printf("ptm user\n");
}
