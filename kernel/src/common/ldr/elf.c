#include <common/arch.h>
#include <common/ldr/elf.h>
#include <memory/heap.h>
#include <memory/vmm.h>
#include <stddef.h>
#include <string.h>

bool elf_supported(const elf64_elf_header_t* elf_header) {
    if(!elf_header) { return false; }
    if(elf_header->e_ident[0] != 0x7f || elf_header->e_ident[1] != 'E' || elf_header->e_ident[2] != 'L' || elf_header->e_ident[3] != 'F') { return false; }
    if(elf_header->e_ident[ELF_CLASS_IDX] != ELF_CLASS_64_BIT) { return false; }
    if(elf_header->e_ident[ELF_DATA_IDX] != ELF_DATA_2LSB) { return false; }
    if(elf_header->e_machine != EMACHINE_X86_64) { return false; }
    if(elf_header->e_type != ETYPE_EXEC && elf_header->e_type != ETYPE_DYN) { return false; }
    return true;
}

uintptr_t load_elf_exec(process_t* process, const elf64_elf_header_t* elf_header, elf64_elf_loader_info_t* loader_info) {
    elf64_program_header_t* phdrs = (elf64_program_header_t*) ((uintptr_t) elf_header + elf_header->e_phoff);

    loader_info->entry_point = elf_header->e_entry;
    loader_info->phnum = elf_header->e_phnum;
    loader_info->phentsize = elf_header->e_phentsize;
    vm_address_space_switch(process->allocator);

    uintptr_t base_address = 0;

    assert(elf_header->e_phnum != 0xffff && "the number of program headers is too large to fit into e_phnum");

    virt_addr_t min_addr = (virt_addr_t) -1;
    virt_addr_t max_addr = 0;

    for(uint16_t i = 0; i < elf_header->e_phnum; i++) {
        elf64_program_header_t* phdr = &phdrs[i];
        if(phdr->p_type != PTYPE_LOAD) { continue; }
        virt_addr_t seg_start = ALIGN_DOWN(phdr->p_vaddr + base_address, PAGE_SIZE_DEFAULT);
        virt_addr_t seg_end = ALIGN_UP(phdr->p_vaddr + base_address + phdr->p_memsz, PAGE_SIZE_DEFAULT);

        if(seg_start < min_addr) { min_addr = seg_start; }
        if(seg_end > max_addr) { max_addr = seg_end; }
    }

    size_t total_page_count = (max_addr - min_addr) / PAGE_SIZE_DEFAULT;
    printf("load: 0x%llx, entry: 0x%llx, %d pages\n", min_addr, elf_header->e_entry, total_page_count);

    virt_addr_t allocation = vmm_try_alloc_backed(process->allocator, min_addr, total_page_count, VM_ACCESS_USER, VM_CACHE_NORMAL, VM_READ_WRITE, true);
    assert(allocation != 0 && "failed to allocate segment for elf loading");

    for(uint16_t i = 0; i < elf_header->e_phnum; i++) {
        elf64_program_header_t* phdr = &phdrs[i];

        if(phdr->p_type == PTYPE_PHDR) { loader_info->phdr_table = (virt_addr_t) phdr->p_vaddr + base_address; }
        if(phdr->p_type != PTYPE_LOAD) { continue; }

        virt_addr_t segment_vaddr = phdr->p_vaddr + base_address;
        virt_addr_t segment_start = ALIGN_DOWN(segment_vaddr, PAGE_SIZE_DEFAULT);
        virt_addr_t segment_end = ALIGN_UP(segment_vaddr + phdr->p_memsz, PAGE_SIZE_DEFAULT);
        size_t segment_pages = (segment_end - segment_start) / PAGE_SIZE_DEFAULT;

        vm_memcpy(process->allocator, &kernel_allocator, segment_vaddr, (virt_addr_t) elf_header + phdr->p_offset, phdr->p_filesz);

        if(phdr->p_memsz > phdr->p_filesz) { vm_memset(process->allocator, segment_vaddr + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz); }

        vm_flags_t flags = VM_READ_ONLY;
        if(phdr->p_flags & PFLAGS_WRITE) { flags |= VM_READ_WRITE; }
        if(phdr->p_flags & PFLAGS_EXECUTE) { flags |= VM_EXECUTE; }

        printf("elf:   seg[%u] 0x%llx..0x%llx pages=%zu [%c%c%c]\n", i, segment_start, segment_end, segment_pages, 'R', (phdr->p_flags & PFLAGS_WRITE) ? 'W' : '-', (phdr->p_flags & PFLAGS_EXECUTE) ? 'X' : '-');

        for(size_t j = 0; j < segment_pages; j++) { vm_reprotect_page(process->allocator, segment_start + j * PAGE_SIZE_DEFAULT, VM_ACCESS_USER, VM_CACHE_NORMAL, flags); }
    }

    vm_address_space_switch(&kernel_allocator);
    return base_address;
}

elf64_elf_loader_info_t* elf_load(process_t* process, const elf64_elf_header_t* elf_header) {
    assert(elf_supported(elf_header) && "ELF file not supported");

    elf64_elf_loader_info_t* loader_info = heap_alloc(sizeof(elf64_elf_loader_info_t));
    if(!loader_info) { return NULL; }
    memset(loader_info, 0, sizeof(*loader_info));

    assert(elf_header->e_type == ETYPE_EXEC);
    load_elf_exec(process, elf_header, loader_info);
    return loader_info;
}
