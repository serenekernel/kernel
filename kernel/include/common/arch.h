#pragma once
#include <arch/internal/cr.h>
#include <common/interrupts.h>
#include <limine.h>
#include <stddef.h>

void arch_init_bsp();
void arch_init_ap(struct limine_mp_info* info);

const char* arch_get_name(void);
[[noreturn]] void arch_die(void);
void arch_wait_for_interrupt(void);
void arch_memory_barrier(void);
void arch_pause();

void arch_panic_int(interrupt_frame_t* frame);

uint64_t arch_get_flags();
void arch_set_flags(uint64_t flags);

uint32_t arch_get_core_id();
bool arch_is_bsp();

void arch_debug_putc(char c);

// Allows us to disable cr4.SMAP
bool arch_get_uap();
bool arch_disable_uap();
void arch_restore_uap(bool __prev);

// Allows us to disable cr0.WP
bool arch_get_wp();
bool arch_disable_wp();
void arch_restore_wp(bool __prev);

uint32_t arch_get_core_count();
