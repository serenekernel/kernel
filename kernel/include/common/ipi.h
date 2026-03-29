#pragma once

#include <memory/memory.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

typedef enum ipi_type : uint8_t {
    IPI_TYPE_TLB_FLUSH,
    IPI_TYPE_DIE
} ipi_type_t;

typedef struct {
    uint32_t ready;
    uint32_t in_use;
    ipi_type_t type;
    union {
        struct {
            virt_addr_t addr;
            size_t length;
        } tlb_flush;
    } data;
} ipi_t;


void ipi_init_bsp(void);
void ipi_init_ap(void);

void ipi_broadcast_flush_tlb(virt_addr_t addr, size_t length);
void ipi_broadcast_die();
