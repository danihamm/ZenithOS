;
;   S3Trampoline.asm
;   S3 resume trampoline with both 16-bit and 64-bit entry points
;   Copyright (c) 2026 Daniel Hammer
;
;   Copied to physical 0x8000 before entering S3. Two entry points:
;     0x8000: real-mode (16-bit) for legacy BIOS wake
;     0x8100: long-mode (64-bit) for UEFI wake
;

[bits 16]
section .text

; ═════════════════════════════════════════════════════════════════════════
; 16-bit real-mode entry (FirmwareWakingVector = 0x8000)
; ═════════════════════════════════════════════════════════════════════════
global S3TrampolineStart
S3TrampolineStart:
    cli
    cld

    ; Set up DS immediately so we can access the data area
    mov ax, 0x0800
    mov ds, ax

    ; Clear SLP_EN + SLP_TYP in PM1 control registers ASAP.
    ; The chipset may still have these set from S3 entry and will
    ; re-enter sleep if we don't clear them immediately.
    mov dx, [ds:data_pm1a_ctrl - S3TrampolineStart]
    or dx, dx
    jz .no_pm1a_16
    in ax, dx
    and ax, 0xC3FF          ; clear SLP_EN (bit 13) + SLP_TYP (bits 12:10)
    out dx, ax
.no_pm1a_16:
    mov dx, [ds:data_pm1b_ctrl - S3TrampolineStart]
    or dx, dx
    jz .no_pm1b_16
    in ax, dx
    and ax, 0xC3FF
    out dx, ax
.no_pm1b_16:

    ; Progress: reached 16-bit trampoline (write 0xA1 to CMOS 0x72)
    mov al, 0xF2        ; register 0x72 | 0x80 (NMI disable)
    out 0x70, al
    mov al, 0xA1
    out 0x71, al

    ; Enable A20 gate via fast A20 (port 0x92). On real hardware, firmware
    ; may leave A20 disabled after S3 wake, causing odd-megabyte addresses
    ; to wrap. QEMU always has A20 enabled, masking this bug.
    in al, 0x92
    or al, 2        ; set A20 enable bit
    and al, 0xFE    ; clear bit 0 (system reset)
    out 0x92, al

    ; Set up remaining segments and stack (DS already set above for PM1 clear)
    mov ax, 0x0800
    mov es, ax
    mov ss, ax
    mov sp, 0x00F0

    lgdt [ds:gdt_ptr - S3TrampolineStart]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp dword 0x08:(0x8000 + pm32_entry - S3TrampolineStart)

[bits 32]
pm32_entry:
    ; Progress: reached 32-bit mode (0xA2)
    mov al, 0xF2
    out 0x70, al
    mov al, 0xA2
    out 0x71, al

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    ; Load lower 32 bits of CR3 from data area
    mov eax, [0x8000 + data_cr3 - S3TrampolineStart]
    mov cr3, eax

    mov ecx, 0xC0000080     ; IA32_EFER
    rdmsr
    or eax, (1 << 8)        ; LME (Long Mode Enable)
    or eax, (1 << 11)       ; NXE (No-Execute Enable) — kernel page tables
                             ; use NX bits; without NXE, bit 63 in PTEs is
                             ; reserved and any page walk through an NX entry
                             ; triggers a reserved-bit #PF → triple fault.
    or eax, (1 << 0)        ; SCE (Syscall Enable) — must be set before
                             ; returning to code that may handle syscalls.
    wrmsr

    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    ; Progress: entering long mode (0xA3)
    mov al, 0xF2
    out 0x70, al
    mov al, 0xA3
    out 0x71, al

    jmp dword 0x18:(0x8000 + lm64_common - S3TrampolineStart)

; ═════════════════════════════════════════════════════════════════════════════
; Temporary GDT
; ═════════════════════════════════════════════════════════════════════════════
align 16
gdt_start:
    dq 0x0000000000000000       ; 0x00: Null
    dq 0x00CF9A000000FFFF       ; 0x08: 32-bit code
    dq 0x00CF92000000FFFF       ; 0x10: 32-bit data
    dq 0x00209A0000000000       ; 0x18: 64-bit code
    dq 0x0000920000000000       ; 0x20: 64-bit data
gdt_end:

gdt_ptr:
    dw gdt_end - gdt_start - 1
    dd 0x8000 + gdt_start - S3TrampolineStart

; 64-bit GDT pointer (lgdt in long mode reads 10 bytes: 2-byte limit + 8-byte base)
gdt_ptr64:
    dw gdt_end - gdt_start - 1
    dq 0x8000 + gdt_start - S3TrampolineStart


; ═════════════════════════════════════════════════════════════════════════
; 64-bit entry at offset 0x100 (X_FirmwareWakingVector = 0x8100)
; UEFI firmware jumps here already in long mode with its own page tables.
; ═════════════════════════════════════════════════════════════════════════
[bits 64]
times (0x100 - ($ - S3TrampolineStart)) db 0x90

global S3Trampoline64
S3Trampoline64:
    cli
    cld

    ; CRITICAL: Clear SLP_EN + SLP_TYP immediately (same as 16-bit path)
    mov dx, [0x8000 + data_pm1a_ctrl - S3TrampolineStart]
    or dx, dx
    jz .no_pm1a_64
    in ax, dx
    and ax, 0xC3FF
    out dx, ax
.no_pm1a_64:
    mov dx, [0x8000 + data_pm1b_ctrl - S3TrampolineStart]
    or dx, dx
    jz .no_pm1b_64
    in ax, dx
    and ax, 0xC3FF
    out dx, ax
.no_pm1b_64:

    ; Progress: reached 64-bit trampoline (0xB1)
    mov al, 0xF2
    out 0x70, al
    mov al, 0xB1
    out 0x71, al

    ; Enable NXE and SCE in EFER before loading kernel CR3.
    ; Kernel page tables use NX bits — without NXE, loading CR3
    ; would cause reserved-bit #PF on first page walk.
    mov ecx, 0xC0000080     ; IA32_EFER
    rdmsr
    or eax, (1 << 11)       ; NXE
    or eax, (1 << 0)        ; SCE
    wrmsr

    ; Load kernel PML4 (full 64-bit) - firmware identity-maps low memory.
    ; Do NOT load the trampoline GDT here — UEFI's CS selector (e.g. 0x38)
    ; would be out of bounds in our small GDT, causing #GP on the next
    ; instruction fetch. UEFI's segments are fine for these few instructions;
    ; AcpiResumeLongMode will load the kernel GDT and reload all segments.
    mov rax, [0x8000 + data_cr3 - S3TrampolineStart]
    mov cr3, rax

    ; Load state pointer and jump to resume (same as lm64_common below,
    ; but we skip the segment reload which needs the trampoline GDT).
    mov rdi, [0x8000 + data_state_ptr - S3TrampolineStart]
    mov rax, [0x8000 + data_resume_addr - S3TrampolineStart]
    jmp rax

; ═════════════════════════════════════════════════════════════════════════════
; Common 64-bit path for 16-bit → 32-bit → 64-bit transition
; ═════════════════════════════════════════════════════════════════════════════
; The real-mode path arrives here with the trampoline GDT active,
; so selector 0x20 (64-bit data) is valid.
lm64_common:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov rdi, [0x8000 + data_state_ptr - S3TrampolineStart]
    mov rax, [0x8000 + data_resume_addr - S3TrampolineStart]
    jmp rax


; ═════════════════════════════════════════════════════════════════════════
; Data area (patched by kernel before S3)
; ═════════════════════════════════════════════════════════════════════════
align 8
global S3TrampolineData
S3TrampolineData:
data_cr3:         dq 0    ; +0:  kernel PML4 physical address (full 64-bit)
data_state_ptr:   dq 0    ; +8:  virtual address of CpuState
data_resume_addr: dq 0    ; +16: virtual address of AcpiResumeLongMode
data_pm1a_ctrl:   dw 0    ; +24: PM1a control block I/O port
data_pm1b_ctrl:   dw 0    ; +26: PM1b control block I/O port

global S3TrampolineEnd
S3TrampolineEnd:
