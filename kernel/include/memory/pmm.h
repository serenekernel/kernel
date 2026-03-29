#pragma once

#include <memory/memory.h>

#define PMM_FLAG_NONE (0)
#define PMM_FLAG_ZERO (1 << 0)

typedef uint8_t pmm_flags_t;

void pmm_init(void);
phys_addr_t pmm_alloc_page(pmm_flags_t flags);
void pmm_free_page(phys_addr_t addr);
