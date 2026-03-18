#pragma once

#include <arch/interrupts.h>

typedef void (*fn_interrupt_handler)(interrupt_frame_t* frame);
extern fn_interrupt_handler interrupt_handlers[256];

void interrupts_register_handler(int vector, fn_interrupt_handler handler);
void interrupts_unregister_handler(int vector);
void interrupts_enable();
void interrupts_disable();
bool interrupts_enabled();

void interrupts_setup_bsp();
void interrupts_setup_ap();
