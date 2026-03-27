/*
    * montauk_main.c
    * MontaukOS entry point and POSIX compat functions for TCC
    * Copyright (c) 2026 Daniel Hammer
*/

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ====================================================================
   Raw syscall wrappers
   ==================================================================== */

static inline long _sys1(long nr, long a1) {
    long ret;
    __asm__ volatile(
        "mov %[a1], %%rdi\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(nr), [a1] "r"(a1)
        : "rcx", "r11", "rdi", "rsi", "rdx", "r8", "r9", "r10", "memory");
    return ret;
}

static inline long _sys2(long nr, long a1, long a2) {
    long ret;
    __asm__ volatile(
        "mov %[a1], %%rdi\n\t"
        "mov %[a2], %%rsi\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(nr), [a1] "r"(a1), [a2] "r"(a2)
        : "rcx", "r11", "rdi", "rsi", "rdx", "r8", "r9", "r10", "memory");
    return ret;
}

#define SYS_GETSIZE 8
#define SYS_GETARGS 25

/* ====================================================================
   fdopen - wrap a raw fd in a FILE (needed by tccelf.c)
   ==================================================================== */

FILE *fdopen(int fd, const char *mode) {
    (void)mode;
    if (fd < 0) return NULL;

    unsigned long fileSize = (unsigned long)_sys1(SYS_GETSIZE, (long)fd);

    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (f == NULL) return NULL;

    f->handle = fd;
    f->pos = 0;
    f->size = fileSize;
    f->eof = 0;
    f->error = 0;
    f->is_std = 0;
    f->ungetc_buf = -1;
    return f;
}

/* ====================================================================
   strtoll / strtoull (needed by TCC, not in MontaukOS libc)
   ==================================================================== */

long long strtoll(const char *nptr, char **endptr, int base) {
    return (long long)strtol(nptr, endptr, base);
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    return (unsigned long long)strtoul(nptr, endptr, base);
}

/* ====================================================================
   _start entry point
   ==================================================================== */

int main(int argc, char **argv);

void _start(void) {
    /* Get command-line arguments from kernel */
    char argbuf[256];
    int len = (int)_sys2(SYS_GETARGS, (long)argbuf, sizeof(argbuf));

    /* Parse into argv[] -- split on spaces, simple tokenizer */
    char *argv[32];
    int argc = 0;

    /* argv[0] = program name */
    argv[argc++] = "tcc";

    if (len > 0) {
        argbuf[len < 255 ? len : 255] = '\0';
        char *p = argbuf;
        while (*p && argc < 31) {
            while (*p == ' ') p++;
            if (*p == '\0') break;

            if (*p == '"') {
                p++;
                argv[argc++] = p;
                while (*p && *p != '"') p++;
            } else {
                argv[argc++] = p;
                while (*p && *p != ' ') p++;
            }
            if (*p) *p++ = '\0';
        }
    }
    argv[argc] = NULL;

    int ret = main(argc, argv);
    exit(ret);
}
