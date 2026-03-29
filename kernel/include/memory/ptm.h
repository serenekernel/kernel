#pragma once

#include <memory/memory.h>
#include <memory/vm.h>

void ptm_init_kernel_bsp();
void ptm_init_kernel_ap();
void ptm_init_user(vm_address_space_t* address_space);

void ptm_map(vm_address_space_t* address_space, virt_addr_t vaddr, phys_addr_t paddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global);
void ptm_rewrite(vm_address_space_t* address_space, uintptr_t vaddr, size_t length, vm_protection_t prot, vm_cache_t cache, vm_privilege_t privilege, bool global);
void ptm_unmap(vm_address_space_t* address_space, uintptr_t vaddr, size_t length);

void ptm_load_address_space(vm_address_space_t* address_space);
bool ptm_physical(vm_address_space_t* address_space, uintptr_t vaddr, uintptr_t* paddr);

void ptm_flush_page(virt_addr_t vaddr, size_t length);
