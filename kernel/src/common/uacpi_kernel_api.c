#include <common/requests.h>
#include <memory/memory.h>
#include <memory/vm.h>
#include <stdio.h>
#include <uacpi/kernel_api.h>
#include <uacpi/log.h>
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
void* uacpi_kernel_map(uacpi_phys_addr paddr, uacpi_size length) {
    LOG_INFO("uacpi_kernel_map: mapping addr=%p, length=%zu\n", paddr, length);
    const uacpi_phys_addr aligned_paddr = ALIGN_DOWN(paddr, PAGE_SIZE_DEFAULT);
    const uacpi_size alignment_diff = paddr - aligned_paddr;
    const uacpi_size aligned_length = ALIGN_UP(length + alignment_diff, PAGE_SIZE_DEFAULT);

    virt_addr_t vaddr = (virt_addr_t) vm_map_direct(g_global_address_space, VM_NO_HINT, aligned_length, VM_PROT_RW, VM_CACHE_NORMAL, aligned_paddr, VM_FLAG_NONE);

    return (void*) (vaddr + alignment_diff);
}

/**
 * Unmap a virtual memory range at 'addr' with a length of 'len' bytes.
 *
 * NOTE: 'addr' may be misaligned, see the comment above 'uacpi_kernel_map'.
 *       Similar steps to uacpi_kernel_map can be taken to retrieve the
 *       virtual address originally returned by the VMM for this mapping
 *       as well as its true length.
 */
void uacpi_kernel_unmap(void* addr, uacpi_size length) {
    LOG_INFO("uacpi_kernel_unmap: unmapping addr=%p, length=%zu\n", addr, length);
    const uintptr_t aligned_addr = ALIGN_DOWN(addr, PAGE_SIZE_DEFAULT);
    const uacpi_size alignment_diff = (uintptr_t) addr - aligned_addr;
    const uacpi_size aligned_length = ALIGN_UP(length + alignment_diff, PAGE_SIZE_DEFAULT);

    vm_unmap(g_global_address_space, (void*) aligned_addr, aligned_length);
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
    switch(level) {
        case UACPI_LOG_ERROR: nl_printf(COLORIZE("fail |", "91") " uacpi: "); break;
        case UACPI_LOG_WARN:  nl_printf(COLORIZE("warn |", "93") " uacpi: "); break;
        case UACPI_LOG_DEBUG: [[fallthrough]];
        case UACPI_LOG_TRACE: [[fallthrough]];
        case UACPI_LOG_INFO:  nl_printf(COLORIZE("info |", "96") " uacpi: "); break;
    }
    nl_vprintf(fmt, args);
    stdio_unlock();
}
