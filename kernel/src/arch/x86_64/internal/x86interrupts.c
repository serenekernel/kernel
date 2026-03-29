#include <arch/cpu_local.h>
#include <arch/hardware/lapic.h>
#include <arch/internal/gdt.h>
#include <arch/x86_64/arch/interrupts.h>
#include <common/arch.h>
#include <common/interrupts.h>
#include <common/irql.h>
#include <memory/memory.h>
#include <memory/vm.h>

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
    interrupts_enable();
    fn_interrupt_handler handler = interrupt_handlers[frame->vector];
    if(handler) { handler(frame); }

    irql_lower(__irql);
}


void x86_64_dispatch_exception(interrupt_frame_t* frame) {
    arch_restore_uap(true);

    if(CPU_LOCAL_READ(faultable.enabled) && (frame->vector == 0x0D || frame->vector == 0x0E)) {
        if(x86_64_fred_enabled()) {
            fred_frame_t* fred_frame = (fred_frame_t*) frame->internal_frame;
            fred_frame->rip = CPU_LOCAL_READ(faultable.redirect);
        } else {
            idt_frame_t* idt_frame = (idt_frame_t*) frame->internal_frame;
            idt_frame->rip = CPU_LOCAL_READ(faultable.redirect);
        }

        return;
    }

    if(frame->vector == 0x0E) {
        vm_fault_reason_t reason = VM_FAULT_UKKNOWN;
        if((frame->error & (1 << 0)) == 0) { reason = VM_FAULT_NOT_PRESENT; }
        if(vm_fault(frame->interrupt_data, reason)) { return; }
        arch_panic_int(frame);
    }

    arch_panic_int(frame);
}

void x86_64_set_rsp0_stack(virt_addr_t stack) {
    tss_t* tss = CPU_LOCAL_READ(cpu_tss);
    tss->rsp0 = stack;
}
