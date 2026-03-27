#ifndef _LIBC_DIRENT_H
#define _LIBC_DIRENT_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define NAME_MAX   255
#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

struct dirent {
    unsigned char d_type;
    char d_name[NAME_MAX + 1];
};

typedef struct _DIR DIR;

DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
void           rewinddir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif /* _LIBC_DIRENT_H */
