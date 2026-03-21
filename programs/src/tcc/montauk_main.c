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

#define SYS_OPEN    6
#define SYS_GETSIZE 8
#define SYS_CLOSE   9
#define SYS_GETARGS 25

/* ====================================================================
   access() - check if a file exists
   ==================================================================== */

int access(const char *path, int mode) {
    (void)mode;
    if (path == NULL) return -1;
    int h = (int)_sys1(SYS_OPEN, (long)path);
    if (h < 0) return -1;
    _sys1(SYS_CLOSE, (long)h);
    return 0;
}

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
   Floating point support (needed by TCC for parsing float literals)
   ==================================================================== */

double ldexp(double x, int n) {
    /* Use bit manipulation via union for exact results */
    if (x == 0.0 || n == 0) return x;
    while (n > 0) { x *= 2.0; n--; }
    while (n < 0) { x *= 0.5; n++; }
    return x;
}

long double ldexpl(long double x, int n) {
    return (long double)ldexp((double)x, n);
}

double strtod(const char *nptr, char **endptr) {
    double result = 0.0;
    double sign = 1.0;
    const char *s = nptr;

    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '-') { sign = -1.0; s++; }
    else if (*s == '+') s++;

    /* Integer part */
    while (*s >= '0' && *s <= '9')
        result = result * 10.0 + (*s++ - '0');

    /* Fractional part */
    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (*s >= '0' && *s <= '9') {
            result += (*s++ - '0') * frac;
            frac *= 0.1;
        }
    }

    /* Exponent */
    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_sign = 1, exp_val = 0;
        if (*s == '-') { exp_sign = -1; s++; }
        else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9')
            exp_val = exp_val * 10 + (*s++ - '0');
        double exp_mult = 1.0;
        for (int i = 0; i < exp_val; i++)
            exp_mult *= 10.0;
        if (exp_sign > 0) result *= exp_mult;
        else result /= exp_mult;
    }

    /* Hex float (0x...p...) */
    if (nptr[0] == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
        s = nptr + 2;
        result = 0.0;
        while (1) {
            int d;
            if (*s >= '0' && *s <= '9') d = *s - '0';
            else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
            else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
            else break;
            result = result * 16.0 + d;
            s++;
        }
        if (*s == '.') {
            s++;
            double frac = 1.0 / 16.0;
            while (1) {
                int d;
                if (*s >= '0' && *s <= '9') d = *s - '0';
                else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
                else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
                else break;
                result += d * frac;
                frac /= 16.0;
                s++;
            }
        }
        if (*s == 'p' || *s == 'P') {
            s++;
            int exp_sign = 1, exp_val = 0;
            if (*s == '-') { exp_sign = -1; s++; }
            else if (*s == '+') s++;
            while (*s >= '0' && *s <= '9')
                exp_val = exp_val * 10 + (*s++ - '0');
            result = ldexp(result, exp_sign * exp_val);
        }
    }

    if (endptr) *endptr = (char *)s;
    return sign * result;
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
