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

    mov ax, 0x0800
    mov ds, ax
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

    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    jmp dword 0x18:(0x8000 + lm64_common - S3TrampolineStart)

; ── Temporary GDT ──────────────────────────────────────────────────────
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

    ; Load kernel PML4 (full 64-bit) - firmware identity-maps low memory
    mov rax, [0x8000 + data_cr3 - S3TrampolineStart]
    mov cr3, rax

    ; Fall through to common path

; ── Common 64-bit path (both entries converge here) ──────────────────
lm64_common:
    mov rdi, [0x8000 + data_state_ptr - S3TrampolineStart]
    mov rax, [0x8000 + data_resume_addr - S3TrampolineStart]
    jmp rax


; ═════════════════════════════════════════════════════════════════════════
; Data area (patched by kernel before S3)
; ═════════════════════════════════════════════════════════════════════════
align 8
global S3TrampolineData
S3TrampolineData:
data_cr3:         dq 0    ; kernel PML4 physical address (full 64-bit)
data_state_ptr:   dq 0    ; virtual address of CpuState
data_resume_addr: dq 0    ; virtual address of AcpiResumeLongMode

global S3TrampolineEnd
S3TrampolineEnd:
