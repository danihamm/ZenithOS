/*
    * crt1.c
    * Minimal C runtime startup for MontaukOS userspace programs
    * Copyright (c) 2026 Daniel Hammer
    *
    * This startup shim is intentionally small: it fetches the raw
    * command-line buffer from the kernel, tokenizes it into argc/argv,
    * calls main(), then exits with main()'s return code.
*/

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

extern int main(int argc, char** argv);

void _start(void) {
    char argbuf[256];
    int len = (int)_sys2(SYS_GETARGS, (long)argbuf, (long)sizeof(argbuf));

    char* argv[32];
    int argc = 0;

    argv[argc++] = (char*)"prog";

    if (len > 0) {
        if (len > 255) {
            len = 255;
        }

        argbuf[len] = '\0';

        char* p = argbuf;
        while (*p != '\0' && argc < 31) {
            while (*p == ' ') {
                p++;
            }

            if (*p == '\0') {
                break;
            }

            argv[argc++] = p;

            while (*p != '\0' && *p != ' ') {
                p++;
            }

            if (*p != '\0') {
                *p++ = '\0';
            }
        }
    }

    argv[argc] = 0;

    _sys1(SYS_EXIT, (long)main(argc, argv));
    __builtin_unreachable();
}
