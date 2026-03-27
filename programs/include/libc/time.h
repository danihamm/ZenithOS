#ifndef _LIBC_TIME_H
#define _LIBC_TIME_H

#pragma once

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long clock_t;

#define CLOCKS_PER_SEC 1000L

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

clock_t    clock(void);
time_t     time(time_t *tloc);
double     difftime(time_t time1, time_t time0);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
time_t     mktime(struct tm *tm);
size_t     strftime(char *s, size_t max, const char *format, const struct tm *tm);

#ifdef __cplusplus
}
#endif

#endif /* _LIBC_TIME_H */
