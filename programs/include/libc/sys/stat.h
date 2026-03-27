#ifndef _LIBC_SYS_STAT_H
#define _LIBC_SYS_STAT_H

#pragma once

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000

#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)

struct stat {
    mode_t        st_mode;
    unsigned long st_size;
    time_t        st_mtime;
};

int mkdir(const char *path, unsigned int mode);
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);

#ifdef __cplusplus
}
#endif

#endif /* _LIBC_SYS_STAT_H */
