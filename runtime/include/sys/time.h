/* sys/time.h — POSIX timeval + gettimeofday + setitimer decls.
 *
 * gettimeofday is implemented as a thin wrapper around the existing
 * clock_gettime ECALL. struct itimerval / setitimer are present so
 * code can compile, but setitimer is a no-op stub (we have no host
 * signal plumbing yet). */
#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include <stddef.h>

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

#define ITIMER_REAL     0
#define ITIMER_VIRTUAL  1
#define ITIMER_PROF     2

int gettimeofday(struct timeval *tv, void *tz);
int setitimer(int which, const struct itimerval *new_value,
              struct itimerval *old_value);
int getitimer(int which, struct itimerval *cur);

#endif /* _SYS_TIME_H */
