/*
    * crt1.c
    * C runtime startup for TCC-compiled programs on MontaukOS
    * Copyright (c) 2026 Daniel Hammer
    *
    * Provides _start, which:
    *   1. Gets command-line arguments from kernel
    *   2. Parses them into argc/argv
    *   3. Calls main(argc, argv)
    *   4. Calls exit(return value)
*/

/* Syscall wrappers (inline asm, no header dependencies) */

static inline long _sys0(long nr) {
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(nr)
        : "rcx", "r11", "rdi", "rsi", "rdx", "r8", "r9", "r10", "memory");
    return ret;
}

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

#define SYS_EXIT    0
#define SYS_GETARGS 25

extern int main(int argc, char **argv);

void _start(void) {
    char argbuf[256];
    int len = (int)_sys2(SYS_GETARGS, (long)argbuf, (long)sizeof(argbuf));

    char *argv[32];
    int argc = 0;

    /* argv[0] = program name */
    argv[argc++] = "prog";

    if (len > 0) {
        if (len > 255) len = 255;
        argbuf[len] = '\0';
        char *p = argbuf;
        while (*p && argc < 31) {
            while (*p == ' ') p++;
            if (*p == '\0') break;
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = '\0';
        }
    }
    argv[argc] = (void*)0;

    int ret = main(argc, argv);
    _sys1(SYS_EXIT, (long)ret);
    __builtin_unreachable();
}
