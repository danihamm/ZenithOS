/*
    * montauk_compat.h
    * POSIX compatibility shims for TCC on MontaukOS
    * Copyright (c) 2026 Daniel Hammer
*/

#ifndef MONTAUK_COMPAT_H
#define MONTAUK_COMPAT_H

#include <stddef.h>
#include <stdint.h>

/* ====================================================================
   setjmp / longjmp (implemented in setjmp.S)
   ==================================================================== */

typedef unsigned long jmp_buf[8]; /* rbx, rbp, r12-r15, rsp, rip */

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

/* ====================================================================
   time stubs (for __DATE__ / __TIME__ macros)
   ==================================================================== */

typedef long time_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

static inline time_t time(time_t *t) {
    if (t) *t = 0;
    return 0;
}

static inline struct tm *localtime(const time_t *t) {
    static struct tm tm0 = {
        .tm_sec = 0, .tm_min = 0, .tm_hour = 12,
        .tm_mday = 1, .tm_mon = 0, .tm_year = 126, /* 2026 */
        .tm_wday = 4, .tm_yday = 0, .tm_isdst = 0
    };
    (void)t;
    return &tm0;
}

/* ====================================================================
   gettimeofday stub (for timing in tcc.c)
   ==================================================================== */

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

static inline int gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (tv) { tv->tv_sec = 0; tv->tv_usec = 0; }
    (void)tz;
    return 0;
}

/* getcwd is provided by libc now */
char *getcwd(char *buf, size_t size);

/* ====================================================================
   POSIX file access stubs
   ==================================================================== */

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

/* Check if file exists by trying to open it */
int access(const char *path, int mode);

/* chmod - no-op on MontaukOS */
static inline int chmod(const char *path, unsigned int mode) {
    (void)path; (void)mode;
    return 0;
}

/* close (provided by libc) */
int close(int fd);

/* lseek - provided by libc, tracks fd positions */
long lseek(int fd, long offset, int whence);

/* realpath - just return a copy of the path */
static inline char *realpath(const char *path, char *resolved) {
    if (path == NULL) return NULL;
    if (resolved == NULL) {
        size_t len = 0;
        const char *p = path;
        while (*p++) len++;
        resolved = (char*)malloc(len + 1);
        if (!resolved) return NULL;
    }
    {
        const char *s = path;
        char *d = resolved;
        while ((*d++ = *s++));
    }
    return resolved;
}

/* ====================================================================
   strtof / strtold stubs
   ==================================================================== */

/* These are declared extern in tcc.h for non-Win32.
   We provide no-op stubs since the libc has no FP parse. */
static inline float strtof(const char *nptr, char **endptr) {
    (void)nptr;
    if (endptr) *endptr = (char*)nptr;
    return 0.0f;
}

static inline long double strtold(const char *nptr, char **endptr) {
    (void)nptr;
    if (endptr) *endptr = (char*)nptr;
    return 0.0L;
}

/* dlfcn stubs not needed -- TCC_IS_NATIVE is disabled for MontaukOS */

/* ====================================================================
   ssize_t (used in tccelf.c)
   ==================================================================== */

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long ssize_t;
#endif

/* ====================================================================
   POSIX I/O (provided by libc)
   ==================================================================== */

int read(int fd, void *buf, size_t count);
int write(int fd, const void *buf, size_t count);

/* unlink = remove */
static inline int unlink(const char *path) {
    return remove(path);
}

/* fputc (should be in libc but isn't yet) */
static inline int fputc(int c, FILE *stream) {
    unsigned char ch = (unsigned char)c;
    size_t n = fwrite(&ch, 1, 1, stream);
    return n == 1 ? c : -1;
}

/* fdopen - MontaukOS doesn't have fd-to-FILE conversion.
   TCC uses fdopen after creating a file with open().
   We return NULL and let TCC fall back to error path,
   or we can use a workaround. */
FILE *fdopen(int fd, const char *mode);

/* strerror */
static inline const char *strerror(int errnum) {
    (void)errnum;
    return "error";
}

/* ====================================================================
   String functions missing from MontaukOS libc
   ==================================================================== */

static inline char *strpbrk(const char *s, const char *accept) {
    for (; *s; s++) {
        for (const char *a = accept; *a; a++) {
            if (*s == *a) return (char *)s;
        }
    }
    return NULL;
}

/* ====================================================================
   Process stubs
   ==================================================================== */

static inline int execvp(const char *file, char *const argv[]) {
    (void)file; (void)argv;
    return -1;
}

/* ====================================================================
   Floating-point parsing and math
   ==================================================================== */

double strtod(const char *nptr, char **endptr);
double ldexp(double x, int n);
long double ldexpl(long double x, int n);

#endif /* MONTAUK_COMPAT_H */
