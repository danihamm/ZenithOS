;
;   Context.asm
;   Context switch: save/restore callee-saved registers, stack pointer, CR3, and FPU state
;   Copyright (c) 2025 Daniel Hammer
;

[bits 64]
section .text

; void SchedContextSwitch(uint64_t* oldRsp, uint64_t newRsp, uint64_t newCR3,
;                         uint8_t* oldFpuArea, uint8_t* newFpuArea)
;   rdi = pointer to save old RSP
;   rsi = new RSP to restore
;   rdx = new PML4 physical address (for CR3)
;   rcx = old FPU state area (may be null)
;   r8  = new FPU state area (may be null)
global SchedContextSwitch
SchedContextSwitch:
    ; Save FPU state before pushing registers (rcx/r8 are caller-saved)
    test rcx, rcx
    jz .skip_fxsave
    fxsave [rcx]
.skip_fxsave:

    ; Stash r8 in r9 (callee-saved registers will clobber r8 slot on stack)
    mov r9, r8

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

    ; Restore FPU state
    test r9, r9
    jz .skip_fxrstor
    fxrstor [r9]
.skip_fxrstor:

    ret
