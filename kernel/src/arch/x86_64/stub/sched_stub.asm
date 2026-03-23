global __context_switch

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
