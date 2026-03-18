#include <arch/internal/cpuid.h>
#include <arch/internal/cr.h>
#include <arch/internal/gdt.h>
#include <common/cpu_local.h>
#include <common/io.h>
#include <common/requests.h>
#include <memory/memory.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
#include <stdio.h>

const char* arch_get_name(void) {
    return "x86_64";
}

[[noreturn]] void arch_die(void) {
    for(;;) {
        __asm__ volatile("cli");
        __asm__ volatile("hlt");
    }
}

void arch_memory_barrier(void) {
    __asm__ volatile("mfence" ::: "memory");
}

void arch_pause() {
    __asm__ volatile("pause" ::: "memory");
}

bool arch_uap_supported() {
    static bool checked = false;
    static bool supported = false;
    if(!checked) {
        supported = (__cpuid_is_feature_supported(CPUID_FEATURE_SMAP) != 0);
        checked = true;
    }
    return supported;
}

void __set_uap(bool value) {
    if(!arch_uap_supported()) { return; }
    arch_memory_barrier();

    // @note: AC flag is inverted for enabling/disabling access checks
    // clear = enable access check
    // set = disable access check
    if(value) {
        __asm__ volatile("clac");
    } else {
        __asm__ volatile("stac");
    }

    arch_memory_barrier();
}

bool arch_get_uap() {
    if(!arch_uap_supported()) { return false; }
    // read rflags
    uint64_t rflags;
    __asm__ volatile("pushfq\n" "pop %0" : "=r"(rflags));
    return (rflags & (1 << 18)) != 0;
}

bool arch_disable_uap() {
    if(!arch_uap_supported()) { return false; }
    bool prev = arch_get_uap();
    __set_uap(false);
    return prev;
}

void arch_restore_uap(bool __prev) {
    if(!arch_uap_supported()) { return; }
    __set_uap(__prev);
}


void arch_wait_for_interrupt(void) {
    __asm__ volatile("hlt");
}

uint32_t arch_get_core_id() {
    return 0;
    // return lapic_get_id();
}

bool arch_is_bsp() {
    return true;
    // return lapic_is_bsp();
}

uint64_t arch_get_flags() {
    uint64_t rflags;
    __asm__ volatile("pushfq\n" "popq %0\n" : "=r"(rflags));
    return rflags;
}

void arch_set_flags(uint64_t flags) {
    __asm__ volatile("pushq %0\n" "popfq\n" : : "r"(flags));
}


size_t arch_get_max_cpu_id(void) {
    uint32_t highest_apic_id = 0;
    for(size_t i = 0; i < mp_request.response->cpu_count; i++) {
        if(mp_request.response->cpus[i]->lapic_id > highest_apic_id) { highest_apic_id = mp_request.response->cpus[i]->lapic_id; }
    }
    return (size_t) highest_apic_id;
}

// void lapic_send_raw_ipi(uint32_t apic_id);
// void lapic_broadcast_raw_ipi();

// void arch_ipi_send_raw(uint32_t cpu_id) {
//     lapic_send_raw_ipi(cpu_id);
// }

// void arch_ipi_broadcast_raw() {
//     lapic_broadcast_raw_ipi();
// }

// void arch_ipi_eoi(void) {
//     lapic_eoi();
// }

void arch_debug_putc(char c) {
    port_write_u8(0xe9, (uint8_t) c);
}

uint64_t __stack_chk_guard = 0xdeadbeefcafebabe;

__attribute__((noreturn)) void __stack_chk_fail(void) {
    // printf("Stack smashing detected on CPU %u\n", lapic_get_id());
    arch_die();
}
