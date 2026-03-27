#ifndef _LIBC_UNISTD_H
#define _LIBC_UNISTD_H

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

int    read(int fd, void *buf, size_t count);
int    write(int fd, const void *buf, size_t count);
int    close(int fd);
long   lseek(int fd, long offset, int whence);
int    chdir(const char *path);
char  *getcwd(char *buf, size_t size);
int    access(const char *path, int mode);
int    isatty(int fd);

unsigned int sleep(unsigned int seconds);

#ifdef __cplusplus
}
#endif

#endif /* _LIBC_UNISTD_H */
