;
;   gdt.asm
;   Copyright (c) 2025 Daniel Hammer
;

[bits 64]
section .text ; Text/code section

global ReloadSegments
global LoadGDT
global LoadTR

LoadGDT:
    lgdt [rdi] ; Run LGDT on the contents of 1st C parameter
    ret

ReloadSegments:
    push 0x08 ; CS descriptor
    lea rax, [rel .reload_CS]
    push rax
    retfq
.reload_CS:
    mov ax, 0x10 ; DS descriptor

    ; ds, es, fs, gs, ss are segment registers on x86_64
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ret

LoadTR:
    mov ax, 0x28 ; TSS selector
    ltr ax
    ret
