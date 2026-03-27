#ifndef _LIBC_LOCALE_H
#define _LIBC_LOCALE_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define LC_ALL      0
#define LC_COLLATE  1
#define LC_CTYPE    2
#define LC_MONETARY 3
#define LC_NUMERIC  4
#define LC_TIME     5

struct lconv {
    char *decimal_point;
};

char *setlocale(int category, const char *locale);
struct lconv *localeconv(void);

#ifdef __cplusplus
}
#endif

#endif /* _LIBC_LOCALE_H */
