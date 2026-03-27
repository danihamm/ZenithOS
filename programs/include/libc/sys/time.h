#ifndef _LIBC_SYS_TIME_H
#define _LIBC_SYS_TIME_H

/* Stub header for MontaukOS */

#ifdef __cplusplus
extern "C" {
#endif

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int gettimeofday(struct timeval *tv, struct timezone *tz);

#ifdef __cplusplus
}
#endif

#endif /* _LIBC_SYS_TIME_H */
