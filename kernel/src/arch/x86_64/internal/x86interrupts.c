#include <arch/hardware/lapic.h>
#include <arch/x86_64/arch/interrupts.h>
#include <common/arch.h>
#include <common/irql.h>

extern void setup_idt_bsp();
extern void setup_idt_ap();

bool x86_64_fred_enabled() {
    // @todo
    return false;
}

void interrupts_setup_bsp() {
    setup_idt_bsp();
}

void interrupts_setup_ap() {
    setup_idt_ap();
}


void x86_64_dispatch_interrupt(interrupt_frame_t* frame) {
    arch_restore_uap(true);

    irql_t __irql = irql_raise((frame->vector >> 4) & 0xf);
    lapic_eoi();

    fn_interrupt_handler handler = interrupt_handlers[frame->vector];
    if(handler) { handler(frame); }

    irql_lower(__irql);
}


void x86_64_dispatch_exception(interrupt_frame_t* frame) {
    arch_restore_uap(true);

    arch_panic_int(frame);
}
