;
;   S3Wake.asm
;   S3 suspend/resume: CPU state save and wake trampoline
;   Copyright (c) 2026 Daniel Hammer
;

[bits 64]
section .text

; ═════════════════════════════════════════════════════════════════════════════
; AcpiSaveAndSuspend
; ═════════════════════════════════════════════════════════════════════════════
; extern "C" int AcpiSaveAndSuspend(CpuState* stateArea)
;   rdi = pointer to CpuState structure
;
; Saves all CPU registers into the CpuState structure, then returns 1.
; The caller is expected to enter S3 after this returns.
; When the system resumes, AcpiResumeLongMode jumps to the saved RIP
; with RAX=0, so Suspend() knows it's a resume.
;
; Returns: 1 on initial call (proceed to enter S3)
;          0 on resume from S3
;
; CpuState layout:
;   0x00  RAX      0x40  R8       0x80  RFLAGS
;   0x08  RBX      0x48  R9       0x88  RIP
;   0x10  RCX      0x50  R10      0x90  CR3
;   0x18  RDX      0x58  R11      0x98  CR0
;   0x20  RSI      0x60  R12      0xA0  CR4
;   0x28  RDI      0x68  R13      0xA8  GDT (10 bytes)
;   0x30  RBP      0x70  R14      0xB2  IDT (10 bytes)
;   0x38  RSP      0x78  R15
;
global AcpiSaveAndSuspend
AcpiSaveAndSuspend:
    ; Save all general-purpose registers
    mov [rdi + 0x00], rax
    mov [rdi + 0x08], rbx
    mov [rdi + 0x10], rcx
    mov [rdi + 0x18], rdx
    mov [rdi + 0x20], rsi
    mov [rdi + 0x28], rdi    ; save rdi (pointer to state area)
    mov [rdi + 0x30], rbp
    mov [rdi + 0x38], rsp
    mov [rdi + 0x40], r8
    mov [rdi + 0x48], r9
    mov [rdi + 0x50], r10
    mov [rdi + 0x58], r11
    mov [rdi + 0x60], r12
    mov [rdi + 0x68], r13
    mov [rdi + 0x70], r14
    mov [rdi + 0x78], r15

    ; Save RFLAGS
    pushfq
    pop rax
    mov [rdi + 0x80], rax

    ; Save return address as resume RIP
    mov rax, [rsp]           ; return address on stack
    mov [rdi + 0x88], rax

    ; Save control registers
    mov rax, cr3
    mov [rdi + 0x90], rax
    mov rax, cr0
    mov [rdi + 0x98], rax
    mov rax, cr4
    mov [rdi + 0xA0], rax

    ; Save GDT pointer (offset 0xA8)
    sgdt [rdi + 0xA8]

    ; Save IDT pointer (offset 0xB2)
    sidt [rdi + 0xB2]

    ; Return 1 = "initial save, now enter S3"
    mov rax, 1
    ret


; ═════════════════════════════════════════════════════════════════════════════
; AcpiWakeEntry
; ═════════════════════════════════════════════════════════════════════════════
; This is the actual waking vector target. Firmware jumps here after S3
; resume. It loads the CpuState pointer from a fixed location (set before
; suspend) and falls through to AcpiResumeLongMode.
;
; For 64-bit waking vector (X_FirmwareWakingVector): firmware resumes in
; long mode and jumps directly here. We load rdi from the saved pointer
; and proceed to restore state.
;
; For QEMU with OVMF and similar UEFI firmware, the 64-bit path works.
; Real-mode (32-bit FirmwareWakingVector) wake is not yet supported.
;
global AcpiWakeEntry
AcpiWakeEntry:
    ; Clear direction flag (firmware may have left it set)
    cld

    ; Load the CpuState pointer from the well-known location.
    ; g_wakeStatePtr is set by the C code before entering S3.
    lea rdi, [rel g_wakeStatePtr]
    mov rdi, [rdi]

    ; Fall through to restore all state
    jmp AcpiResumeLongMode


; ═════════════════════════════════════════════════════════════════════════════
; Well-known pointer to CpuState (set before suspend)
; ═════════════════════════════════════════════════════════════════════════════
; Placed in .text so AcpiWakeEntry can use RIP-relative addressing
; without a cross-section relocation warning.
; extern "C" void* g_wakeStatePtr;
global g_wakeStatePtr
g_wakeStatePtr: dq 0

; ═════════════════════════════════════════════════════════════════════════════
; AcpiResumeLongMode
; ═════════════════════════════════════════════════════════════════════════════
; extern "C" void AcpiResumeLongMode(CpuState* stateArea)
;   rdi = pointer to CpuState structure
;
; Restores all saved registers and returns to the original Suspend() caller
; with RAX=0 to indicate "resumed from S3".
;
global AcpiResumeLongMode
AcpiResumeLongMode:
    ; Progress: reached AcpiResumeLongMode (0xC1)
    mov al, 0xF2
    out 0x70, al
    mov al, 0xC1
    out 0x71, al

    ; At entry: rdi = CpuState*, RSP = trampoline stack (unmapped junk),
    ;           CR3 = kernel PML4 (loaded by trampoline), CS = trampoline selector.
    ;
    ; We must restore the kernel stack FIRST so push/pop work, then reload
    ; GDT/CS/IDT before any interrupt can fire.

    ; Restore kernel RSP immediately so we have a valid stack
    mov rsp, [rdi + 0x38]

    ; Restore GDT
    lgdt [rdi + 0xA8]

    ; Reload CS to kernel code selector (0x08) via far return.
    ; Without this, CS still holds the trampoline's selector (0x18)
    ; which maps to UserData in the kernel GDT → #GP on first interrupt.
    push 0x08               ; kernel code selector
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:

    ; Progress: GDT + CS reloaded (0xC2)
    mov al, 0xF2
    out 0x70, al
    mov al, 0xC2
    out 0x71, al

    ; Reload data segment registers with kernel data selector
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Restore IDT
    lidt [rdi + 0xB2]

    ; Restore CR4 before CR3 — PGE, PAE, OSFXSR etc. must be set before
    ; the page table switch. The trampoline only set minimal bits (PAE).
    ; Real hardware needs the full saved CR4 (especially PGE for TLB
    ; correctness and WP in CR0 for write-protect enforcement).
    mov rax, [rdi + 0xA0]
    mov cr4, rax

    ; Restore CR0 (WP, NE, etc.)
    mov rax, [rdi + 0x98]
    mov cr0, rax

    ; Restore saved CR3 (process PML4). The trampoline used the kernel
    ; master PML4 to get here; now switch to the actual saved context.
    mov rax, [rdi + 0x90]
    mov cr3, rax

    ; Progress: IDT + CR4/CR0/CR3 restored (0xC3)
    mov al, 0xF2
    out 0x70, al
    mov al, 0xC3
    out 0x71, al

    ; Restore general-purpose registers (skip RSP — already restored above)
    mov rbx, [rdi + 0x08]
    mov rcx, [rdi + 0x10]
    mov rdx, [rdi + 0x18]
    mov rsi, [rdi + 0x20]
    mov rbp, [rdi + 0x30]
    mov r8,  [rdi + 0x40]
    mov r9,  [rdi + 0x48]
    mov r10, [rdi + 0x50]
    mov r11, [rdi + 0x58]
    mov r12, [rdi + 0x60]
    mov r13, [rdi + 0x68]
    mov r14, [rdi + 0x70]
    mov r15, [rdi + 0x78]

    ; Restore RFLAGS
    mov rax, [rdi + 0x80]
    push rax
    popfq

    ; Progress: about to call AcpiResumeEntry (0xC4)
    mov al, 0xF2
    out 0x70, al
    mov al, 0xC4
    out 0x71, al

    ; Restore rdi last
    mov rdi, [rdi + 0x28]

    ; Instead of returning to Suspend() via ret (which is fragile due to
    ; compiler stack frame assumptions), jump directly to a dedicated C
    ; resume function. This is how Linux and other kernels handle S3 resume.
    extern AcpiResumeEntry
    jmp AcpiResumeEntry
