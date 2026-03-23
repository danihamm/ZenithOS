;
;   SyscallEntry.asm
;   SYSCALL/SYSRET entry point and user-mode transition (SMP-aware)
;   Copyright (c) 2025-2026 Daniel Hammer
;

[bits 64]
section .text

extern SyscallDispatch

; ====================================================================
; Per-CPU data offsets (must match CpuData in SmpBoot.hpp)
; ====================================================================
%define CPUDATA_KERNEL_RSP  8
%define CPUDATA_USER_RSP   16

; ====================================================================
; SyscallEntry -- called by the SYSCALL instruction
;   RCX = user RIP, R11 = user RFLAGS, RAX = syscall number
;   Args: RDI, RSI, RDX, R10, R8, R9
;   Interrupts are masked (FMASK clears IF)
; ====================================================================
global SyscallEntry
SyscallEntry:
    swapgs                              ; GS now points to per-CPU CpuData
    mov [gs:CPUDATA_USER_RSP], rsp      ; save user RSP to per-CPU scratch
    mov rsp, [gs:CPUDATA_KERNEL_RSP]    ; switch to per-CPU kernel stack

    ; Build SyscallFrame on kernel stack (push order matches struct)
    push qword [gs:CPUDATA_USER_RSP]    ; user_rsp
    push rcx                            ; user_rip
    push r11                            ; user_rflags
    push rax                            ; syscall_nr
    push rdi                            ; arg1
    push rsi                            ; arg2
    push rdx                            ; arg3
    push r10                            ; arg4
    push r8                             ; arg5
    push r9                             ; arg6

    ; Callee-saved registers (preserve for user)
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    sti                                 ; safe to take interrupts now

    mov rdi, rsp                        ; arg1 = pointer to SyscallFrame
    call SyscallDispatch                ; returns int64_t in rax

    cli                                 ; disable interrupts for sysret

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    add rsp, 56                         ; skip arg6..arg1 (6*8) + syscall_nr (1*8) = 56

    pop r11                             ; user RFLAGS
    pop rcx                             ; user RIP
    pop rsp                             ; user RSP

    swapgs                              ; restore user GS
    o64 sysret

; ====================================================================
; JumpToUserMode -- initial transition to ring 3 via IRETQ
;   RDI = user RIP (entry point)
;   RSI = user RSP (top of user stack)
; ====================================================================
global JumpToUserMode
JumpToUserMode:
    mov ax, 0x1B                        ; UserData | RPL3
    mov ds, ax
    mov es, ax

    push 0x1B                           ; SS  = UserData | RPL3
    push rsi                            ; RSP = user stack top
    push 0x202                          ; RFLAGS (IF=1)
    push 0x23                           ; CS  = UserCode | RPL3
    push rdi                            ; RIP = entry point
    swapgs                              ; switch from kernel GS to user GS
    iretq
