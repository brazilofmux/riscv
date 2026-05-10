#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC 1000000

#define CLOCK_REALTIME   0
#define CLOCK_MONOTONIC  1

struct timespec {
    time_t       tv_sec;
    long         tv_nsec;
};

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

/* Clock + time retrieval */
time_t  time(time_t *tloc);
clock_t clock(void);
int     clock_gettime(int clock_id, struct timespec *ts);
double  difftime(time_t t1, time_t t0);

/* Sleep */
int nanosleep(const struct timespec *req, struct timespec *rem);

/* Time conversion (UTC-based, no timezone support) */
struct tm *gmtime(const time_t *timep);
struct tm *localtime(const time_t *timep);
time_t     mktime(struct tm *tm);

/* Time formatting */
char  *asctime(const struct tm *tm);
char  *ctime(const time_t *timep);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

#endif
