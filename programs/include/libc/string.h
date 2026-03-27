#ifndef _LIBC_STRING_H
#define _LIBC_STRING_H

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

size_t strlen(const char *s);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
int    strcmp(const char *s1, const char *s2);
int    strncmp(const char *s1, const char *s2, size_t n);
char  *strcpy(char *dest, const char *src);
char  *strncpy(char *dest, const char *src, size_t n);
char  *strcat(char *dest, const char *src);
char  *strncat(char *dest, const char *src, size_t n);
char  *strdup(const char *s);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strpbrk(const char *s, const char *accept);
int    strcasecmp(const char *s1, const char *s2);
int    strncasecmp(const char *s1, const char *s2, size_t n);
int    strcoll(const char *s1, const char *s2);
char  *strstr(const char *haystack, const char *needle);
const char *strerror(int errnum);

#ifdef __cplusplus
}
#endif

#endif /* _LIBC_STRING_H */
