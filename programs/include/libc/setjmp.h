#ifndef _LIBC_SETJMP_H
#define _LIBC_SETJMP_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long jmp_buf[8];

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* _LIBC_SETJMP_H */
