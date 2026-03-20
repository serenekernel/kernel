#include <limine.h>
#include <memory/memory.h>

const char* limine_memmap_type_to_str(uint64_t type) {
    switch(type) {
        case LIMINE_MEMMAP_USABLE:                 return "usable";
        case LIMINE_MEMMAP_RESERVED:               return "reserved";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       return "acpireclaim";
        case LIMINE_MEMMAP_ACPI_NVS:               return "acpinvs";
        case LIMINE_MEMMAP_BAD_MEMORY:             return "badmem";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "ldr reclaim";
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return "exec & modules";
        case LIMINE_MEMMAP_FRAMEBUFFER:            return "fb";
        case LIMINE_MEMMAP_RESERVED_MAPPED:        return "resvmapped";
        default:                                   return "unknown";
    }
}
