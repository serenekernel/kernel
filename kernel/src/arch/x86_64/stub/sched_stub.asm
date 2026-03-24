section .rodata
fldcw_word: dw (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (0b11 << 8)
ldmxcsr_dword: dd (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12)

section .text


global __context_switch
global __userspace_init_sysexit
global __userspace_init_fred

__context_switch:
    push rbx
    push rbp
    push r15
    push r14
    push r13
    push r12

    mov qword [rdi + 0], rsp
    mov rsp, qword [rsi + 0]

    xor r12, r12
    mov ds, r12
    mov es, r12

    pop r12
    pop r13
    pop r14
    pop r15
    pop rbp
    pop rbx

    mov rax, rdi
    ret


__userspace_exit_common:
    ; return address of whatever called us
    ; since we fuck with the stack we gotta grab this rq
    pop r10

    ; INTEL WHY DID YOU MAKE THIS TAKE A MEMORY OPERAND ONLY
    fldcw [fldcw_word]
    ldmxcsr [ldmxcsr_dword]

    pop rcx ; address to sysret to
    pop rax ; userspace stack pointer
    push r10 ; push this back on the stack so ret can use it

    ; bye bye regs
    ; @note: we don't bother clearing rbx, rbp, r12, r13, r14, r15
    ; because they should be cleared by the context switch
    ; we also don't clear rax because it's going to be used for sysret & fred
   	xor rdx, rdx
   	xor rsi, rsi
   	xor rdi, rdi
   	xor r8, r8
   	xor r9, r9
   	xor r10, r10

    mov r11, 0x202 ; interrupts enabled
    ret

; void __userspace_init_sysexit();
global __userspace_init_sysexit
__userspace_init_sysexit:
    cli
    call __userspace_exit_common

    mov rsp, rax
    xor rax, rax

    swapgs
    o64 sysret

; void __userspace_init_fred();
global __userspace_init_fred
__userspace_init_fred:
    cli
    call __userspace_exit_common

    push qword 0x20 | 3
    push qword rax
    push qword r11
    push qword 0x28 | 3
    push qword rcx
    push qword 0

    xor rax, rax
    eretu
