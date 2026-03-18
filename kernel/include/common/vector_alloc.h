#pragma once
#include <common/irql.h>
#include <stdint.h>

uint8_t alloc_interrupt_vector(irql_t target_irql);
uint8_t alloc_specific_interrupt_vector(uint8_t vector);
void free_interrupt_vector(uint8_t vector);
