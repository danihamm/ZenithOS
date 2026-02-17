;
;   Context.asm
;   Context switch: save/restore callee-saved registers, stack pointer, and CR3
;   Copyright (c) 2025 Daniel Hammer
;

[bits 64]
section .text

; void SchedContextSwitch(uint64_t* oldRsp, uint64_t newRsp, uint64_t newCR3)
;   rdi = pointer to save old RSP
;   rsi = new RSP to restore
;   rdx = new PML4 physical address (for CR3)
global SchedContextSwitch
SchedContextSwitch:
    ; Save callee-saved registers on the current stack
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Save current RSP into *oldRsp
    mov [rdi], rsp

    ; Load new RSP
    mov rsp, rsi

    ; Switch address space if CR3 differs (avoid unnecessary TLB flush)
    mov rax, cr3
    cmp rax, rdx
    je .skip_cr3
    mov cr3, rdx
.skip_cr3:

    ; Restore callee-saved registers from the new stack
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret
