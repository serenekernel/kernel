global __probe_msr
global __probe_msr_handler

__probe_msr:
    mov ecx, edi
    rdmsr
    mov eax, 1
    ret
__probe_msr_handler:
    mov eax, 0
    ret
