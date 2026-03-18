#include <assert.h>
#include <common/interrupts.h>
#include <stdint.h>

fn_interrupt_handler interrupt_handlers[256] = { 0 };

void interrupts_register_handler(int vector, fn_interrupt_handler handler) {
    assert(vector >= 0 && vector < 256);
    assert(interrupt_handlers[vector] == 0 && "Interrupt handler already registered for this vector");
    interrupt_handlers[vector] = handler;
}

void interrupts_unregister_handler(int vector) {
    assert(vector >= 0 && vector < 256);
    assert(interrupt_handlers[vector] != 0 && "No interrupt handler registered for this vector");
    interrupt_handlers[vector] = 0;
}

void interrupts_enable() {
    asm volatile("sti");
}

void interrupts_disable() {
    asm volatile("cli");
}

bool interrupts_enabled() {
    uint64_t rflags;
    asm volatile("pushfq\n" "popq %0\n" : "=r"(rflags));
    return (rflags & (1 << 9)) != 0;
}
