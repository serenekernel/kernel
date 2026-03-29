#include <common/arch.h>
#include <common/ldr/elf.h>
#include <memory/heap.h>
#include <memory/vm.h>
#include <stddef.h>
#include <string.h>

#include "common/fs/vfs.h"
#include "common/ldr/elf_internal.h"
#include "memory/memory.h"

bool elf_supported(const elf64_elf_header_t* elf_header) {
    if(!elf_header) { return false; }
    if(elf_header->e_ident[0] != 0x7f || elf_header->e_ident[1] != 'E' || elf_header->e_ident[2] != 'L' || elf_header->e_ident[3] != 'F') { return false; }
    if(elf_header->e_ident[ELF_CLASS_IDX] != ELF_CLASS_64_BIT) { return false; }
    if(elf_header->e_ident[ELF_DATA_IDX] != ELF_DATA_2LSB) { return false; }
    if(elf_header->e_machine != EMACHINE_X86_64) { return false; }
    if(elf_header->e_type != ETYPE_EXEC && elf_header->e_type != ETYPE_DYN) { return false; }
    return true;
}

typedef struct {
    uintptr_t image_start_address;
    uintptr_t image_end_address;
    uintptr_t image_offset;
} elf_image_allocation;

bool __allocate_for_image(process_t* process, const elf64_elf_header_t* elf_header, const elf64_program_header_t* phdrs, elf_image_allocation* allocation) {
    uintptr_t image_start_address = 0;
    uintptr_t image_end_address = 0;
    uintptr_t image_offset = 0;

    for(size_t i = 0; i < elf_header->e_phnum; i++) {
        if(phdrs[i].p_type == PTYPE_LOAD) {
            if(image_start_address == 0 || phdrs[i].p_vaddr < image_start_address) { image_start_address = phdrs[i].p_vaddr; }
            if(image_end_address == 0 || phdrs[i].p_vaddr + phdrs[i].p_memsz > image_end_address) { image_end_address = phdrs[i].p_vaddr + phdrs[i].p_memsz; }
        }
    }

    if(elf_header->e_type == ETYPE_DYN) { image_start_address = 0; }

    LOG_INFO("lowest_address = 0x%lx, highest_address = 0x%lx\n", image_start_address, image_end_address);

    uintptr_t target_allocation = image_start_address + image_offset;
    size_t image_size = ALIGN_UP((image_end_address - image_start_address), PAGE_SIZE_DEFAULT);
    LOG_INFO("target_allocation = 0x%lx, image_size = 0x%lx\n", target_allocation, image_size);

    if(elf_header->e_type == ETYPE_DYN) {
        uintptr_t allocated = (virt_addr_t) vm_map_anon(process->address_space, 0, image_size, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_ZERO);
        assert(allocated != 0 && "Failed to allocate memory for elf image");
        image_offset = allocated;
    } else {
        printf("target_allocation = 0x%lx\n", target_allocation);
        virt_addr_t result_vaddr = (virt_addr_t) vm_map_anon(process->address_space, (void*) target_allocation, image_size, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_ZERO | VM_FLAG_FIXED);
        assert(result_vaddr != 0 && "Failed to allocate memory for elf image");
    }
    LOG_INFO("image_slide = 0x%lx, image_size = 0x%lx\n", image_offset, image_size);

    allocation->image_start_address = image_start_address;
    allocation->image_end_address = image_end_address;
    allocation->image_offset = image_offset;
    return true;
}

bool __elf_load_image(process_t* process, const elf64_elf_header_t* elf_header, vfs_path_t* path, elf64_elf_loader_info_t* loader_info) {
    elf_image_allocation allocation = { 0 };

    // read phdrs into cache
    elf64_program_header_t* phdrs = heap_alloc(sizeof(elf64_program_header_t) * elf_header->e_phnum);
    for(size_t i = 0; i < elf_header->e_phnum; i++) {
        assert(vfs_read(path, &phdrs[i], sizeof(elf64_program_header_t), elf_header->e_phoff + i * elf_header->e_phentsize, nullptr) == VFS_RESULT_OK && "Failed to read program header");
        LOG_INFO("phdr[%d].p_type = 0x%lx\n", i, phdrs[i].p_type);
        LOG_INFO("phdr[%d].p_vaddr = 0x%lx, p_memsz = 0x%lx, p_filesz = 0x%lx\n", i, phdrs[i].p_vaddr, phdrs[i].p_memsz, phdrs[i].p_filesz);
    }

    // allocate memory for image
    assert(__allocate_for_image(process, elf_header, phdrs, &allocation) && "Failed to allocate memory for elf image");

    loader_info->image_offset = allocation.image_offset;
    loader_info->executable_entry_point = allocation.image_offset + elf_header->e_entry;
    loader_info->image_entry_point = allocation.image_offset + elf_header->e_entry;

    loader_info->phnum = elf_header->e_phnum;
    loader_info->phentsize = elf_header->e_phentsize;
    for(size_t i = 0; i < elf_header->e_phnum; i++) {
        if(phdrs[i].p_type == PTYPE_PHDR) {
            loader_info->phdr_table = allocation.image_offset + phdrs[i].p_vaddr;
            break;
        }
    }

    if(loader_info->phdr_table == 0) { loader_info->phdr_table = allocation.image_offset + elf_header->e_phoff; }

    // load PT_LOAD phdrs into memory
    for(size_t i = 0; i < elf_header->e_phnum; i++) {
        if(phdrs[i].p_type != PTYPE_LOAD) { continue; }

        vm_protection_t flags = VM_PROT_NO_ACCESS;
        if(phdrs[i].p_flags & PFLAGS_READ) { flags.read = true; }
        if(phdrs[i].p_flags & PFLAGS_WRITE) { flags.write = true; }
        if(phdrs[i].p_flags & PFLAGS_EXECUTE) { flags.execute = true; }
        virt_addr_t start_vaddr = MATH_FLOOR(allocation.image_offset + phdrs[i].p_vaddr, PAGE_SIZE_DEFAULT);
        virt_addr_t end_vaddr = ALIGN_UP(allocation.image_offset + phdrs[i].p_vaddr + phdrs[i].p_memsz, PAGE_SIZE_DEFAULT);
        for(virt_addr_t j = start_vaddr; j < end_vaddr; j += PAGE_SIZE_DEFAULT) { vm_rewrite_prot(process->address_space, (void*) j, PAGE_SIZE_DEFAULT, flags); }

        virt_addr_t phdr_data = (virt_addr_t) heap_alloc(phdrs[i].p_filesz);
        assert(vfs_read(path, (void*) phdr_data, phdrs[i].p_filesz, phdrs[i].p_offset, nullptr) == VFS_RESULT_OK && "Failed to read program header data");
        LOG_INFO("Loading segment %zu: vaddr=0x%lx, paddr=0x%lx, size=%zu\n", i, allocation.image_offset + phdrs[i].p_vaddr, phdr_data, phdrs[i].p_filesz);
        vm_copy_to(process->address_space, allocation.image_offset + phdrs[i].p_vaddr, (void*) phdr_data, phdrs[i].p_filesz);
        heap_free((void*) phdr_data, phdrs[i].p_filesz);
    }

    // load interp if any
    // @todo: merge this pass with the previous
    for(size_t i = 0; i < elf_header->e_phnum; i++) {
        if(phdrs[i].p_type == PTYPE_INTERP) {
            virt_addr_t phdr_data = (virt_addr_t) heap_alloc(phdrs[i].p_filesz);
            assert(vfs_read(path, (void*) phdr_data, phdrs[i].p_filesz, phdrs[i].p_offset, nullptr) == VFS_RESULT_OK && "Failed to read program header data");
            LOG_INFO("interpreter: %s\n", (const char*) phdr_data);

            elf64_elf_loader_info_t* interp_loader_info;
            if(!elf_load_file(process, &VFS_MAKE_ABS_PATH((const char*) phdr_data), &interp_loader_info)) {
                heap_free((void*) phdr_data, phdrs[i].p_filesz);
                heap_free(phdrs, sizeof(elf64_program_header_t) * elf_header->e_phnum);
                return false;
            }

            loader_info->executable_entry_point = interp_loader_info->executable_entry_point;
            loader_info->interp_base = interp_loader_info->image_offset;
            heap_free(interp_loader_info, sizeof(elf64_elf_loader_info_t));

            heap_free((void*) phdr_data, phdrs[i].p_filesz);
        }
    }

    heap_free(phdrs, sizeof(elf64_program_header_t) * elf_header->e_phnum);
    return true;
}

bool elf_load_file(process_t* process, vfs_path_t* path, elf64_elf_loader_info_t** out_loader_info) {
    vfs_node_attr_t attr;
    if(vfs_attr(path, &attr) != VFS_RESULT_OK) {
        LOG_FAIL("Failed to get size of elf file\n");
        return false;
    }

    assert(attr.type == VFS_NODE_TYPE_FILE && attr.size >= sizeof(elf64_elf_header_t));

    elf64_elf_header_t* elf_data = heap_alloc(sizeof(elf64_elf_header_t));
    if(vfs_read(path, elf_data, sizeof(elf64_elf_header_t), 0, nullptr) != VFS_RESULT_OK) {
        LOG_FAIL("Failed to read elf header\n");
        heap_free(elf_data, sizeof(elf64_elf_header_t));
        return false;
    }
    if(!elf_supported(elf_data)) {
        LOG_FAIL("Unsupported elf file\n");
        heap_free(elf_data, sizeof(elf64_elf_header_t));
        return false;
    }

    elf64_elf_loader_info_t* loader_info = heap_alloc(sizeof(elf64_elf_loader_info_t));
    if(!loader_info) {
        LOG_FAIL("Failed to allocate loader info\n");
        heap_free(elf_data, sizeof(elf64_elf_header_t));
        return false;
    }
    memset(loader_info, 0, sizeof(elf64_elf_loader_info_t));

    if(!__elf_load_image(process, elf_data, path, loader_info)) {
        LOG_FAIL("Failed to load elf image\n");
        heap_free(elf_data, sizeof(elf64_elf_header_t));
        heap_free(loader_info, sizeof(elf64_elf_loader_info_t));
        return false;
    }

    *out_loader_info = loader_info;
    return true;
}
