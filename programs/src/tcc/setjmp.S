/*
    * setjmp.S
    * setjmp/longjmp for x86_64 MontaukOS
    * Copyright (c) 2026 Daniel Hammer
    *
    * jmp_buf layout (8 x uint64_t):
    *   [0] = rbx
    *   [1] = rbp
    *   [2] = r12
    *   [3] = r13
    *   [4] = r14
    *   [5] = r15
    *   [6] = rsp (after return from setjmp)
    *   [7] = rip (return address)
*/

.text

.global setjmp
.type setjmp, @function
setjmp:
    /* rdi = jmp_buf */
    mov %rbx,   (%rdi)
    mov %rbp,  8(%rdi)
    mov %r12, 16(%rdi)
    mov %r13, 24(%rdi)
    mov %r14, 32(%rdi)
    mov %r15, 40(%rdi)
    /* rsp as it will be after we return (pop return address) */
    lea 8(%rsp), %rax
    mov %rax,  48(%rdi)
    /* return address */
    mov (%rsp), %rax
    mov %rax,  56(%rdi)
    /* return 0 */
    xor %eax, %eax
    ret

.global longjmp
.type longjmp, @function
longjmp:
    /* rdi = jmp_buf, esi = val */
    mov %esi, %eax
    /* longjmp(buf, 0) must return 1 */
    test %eax, %eax
    jnz 1f
    inc %eax
1:
    mov   (%rdi), %rbx
    mov  8(%rdi), %rbp
    mov 16(%rdi), %r12
    mov 24(%rdi), %r13
    mov 32(%rdi), %r14
    mov 40(%rdi), %r15
    mov 48(%rdi), %rsp
    jmp *56(%rdi)
