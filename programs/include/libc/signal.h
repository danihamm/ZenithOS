#ifndef _LIBC_SIGNAL_H
#define _LIBC_SIGNAL_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);

#define SIGINT 2

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

sighandler_t signal(int sig, sighandler_t handler);
int          raise(int sig);

#ifdef __cplusplus
}
#endif

#endif /* _LIBC_SIGNAL_H */
