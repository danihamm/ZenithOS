/*
    * montauk_compat.h
    * POSIX compatibility shims for TCC on MontaukOS
    * Copyright (c) 2026 Daniel Hammer
*/

#ifndef MONTAUK_COMPAT_H
#define MONTAUK_COMPAT_H

#include <stddef.h>
#include <stdint.h>

/* getcwd is provided by libc now */
char *getcwd(char *buf, size_t size);

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

/* fdopen - MontaukOS doesn't have fd-to-FILE conversion.
   TCC uses fdopen after creating a file with open().
   We return NULL and let TCC fall back to error path,
   or we can use a workaround. */
FILE *fdopen(int fd, const char *mode);

/* ====================================================================
   Process stubs
   ==================================================================== */

static inline int execvp(const char *file, char *const argv[]) {
    (void)file; (void)argv;
    return -1;
}

#endif /* MONTAUK_COMPAT_H */
