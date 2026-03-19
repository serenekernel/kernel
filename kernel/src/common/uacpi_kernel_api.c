#include <common/requests.h>
#include <memory/memory.h>
#include <memory/vmm.h>
#include <stdio.h>
#include <uacpi/kernel_api.h>
#include <uacpi/status.h>

// Returns the PHYSICAL address of the RSDP structure via *out_rsdp_address.
uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
    if(!rsdp_request.response) { return UACPI_STATUS_NOT_FOUND; }
    *out_rsdp_address = FROM_HHDM(rsdp_request.response->address);
    return UACPI_STATUS_OK;
}

/**
 * Map a physical memory range starting at 'addr' with length 'len', and return
 * a virtual address that can be used to access it.
 *
 * NOTE: 'addr' may be misaligned, in this case the host is expected to round it
 *       down to the nearest page-aligned boundary and map that, while making
 *       sure that at least 'len' bytes are still mapped starting at 'addr'. The
 *       return value preserves the misaligned offset.
 *
 *       Example for uacpi_kernel_map(0x1ABC, 0xF00):
 *           1. Round down the 'addr' we got to the nearest page boundary.
 *              Considering a PAGE_SIZE of 4096 (or 0x1000), 0x1ABC rounded down
 *              is 0x1000, offset within the page is 0x1ABC - 0x1000 => 0xABC
 *           2. Requested 'len' is 0xF00 bytes, but we just rounded the address
 *              down by 0xABC bytes, so add those on top. 0xF00 + 0xABC => 0x19BC
 *           3. Round up the final 'len' to the nearest PAGE_SIZE boundary, in
 *              this case 0x19BC is 0x2000 bytes (2 pages if PAGE_SIZE is 4096)
 *           4. Call the VMM to map the aligned address 0x1000 (from step 1)
 *              with length 0x2000 (from step 3). Let's assume the returned
 *              virtual address for the mapping is 0xF000.
 *           5. Add the original offset within page 0xABC (from step 1) to the
 *              resulting virtual address 0xF000 + 0xABC => 0xFABC. Return it
 *              to uACPI.
 */
void* uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    const uacpi_phys_addr aligned_addr = ALIGN_DOWN(addr, PAGE_SIZE_DEFAULT);
    const uacpi_size page_count = ALIGN_UP(len, PAGE_SIZE_DEFAULT) / PAGE_SIZE_DEFAULT;

    virt_addr_t vaddr = vmm_alloc(&kernel_allocator, page_count);
    vm_map_pages_continuous(&kernel_allocator, vaddr, aligned_addr, page_count, VM_ACCESS_KERNEL, VM_CACHE_NORMAL, VM_READ_WRITE);
    return (void*) (vaddr + (addr - aligned_addr));
}

/**
 * Unmap a virtual memory range at 'addr' with a length of 'len' bytes.
 *
 * NOTE: 'addr' may be misaligned, see the comment above 'uacpi_kernel_map'.
 *       Similar steps to uacpi_kernel_map can be taken to retrieve the
 *       virtual address originally returned by the VMM for this mapping
 *       as well as its true length.
 */
void uacpi_kernel_unmap(void* addr, uacpi_size len) {
    const uacpi_phys_addr aligned_addr = ALIGN_DOWN(addr, PAGE_SIZE_DEFAULT);
    const uacpi_size page_count = ALIGN_UP(len, PAGE_SIZE_DEFAULT) / PAGE_SIZE_DEFAULT;
    vm_unmap_pages_continuous(&kernel_allocator, aligned_addr, page_count);
    vmm_free(&kernel_allocator, aligned_addr);
}

UACPI_PRINTF_DECL(2, 3)
void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* fmt, ...) {
    va_list val;
    va_start(val, fmt);
    uacpi_kernel_vlog(level, fmt, val);
    va_end(val);
}

void uacpi_kernel_vlog(uacpi_log_level level, const uacpi_char* fmt, uacpi_va_list args) {
    stdio_lock();
    nl_printf("[uacpi log %d]: ", level);
    nl_vprintf(fmt, args);
    nl_printf("\n");
    stdio_unlock();
}
