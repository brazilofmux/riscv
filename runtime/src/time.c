/*
 * Minimal time implementation for RV32IM runtime.
 * time() and clock() use ecall syscalls to the host.
 * gmtime/localtime/mktime/strftime are self-contained.
 */

#include <time.h>
#include <locale.h>
#include <string.h>
#include <stdio.h>

/* Syscall wrappers defined in time_syscall.s */
extern int _clock_gettime(int clockid, void *tp);
extern long _get_cpu_clock(void);

static struct tm _tm_buf;

/* Days in each month (non-leap, then leap) */
static const int days_in_month[2][12] = {
    { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
    { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

time_t time(time_t *tloc) {
    struct { int tv_sec; int tv_nsec; } ts;
    int rc = _clock_gettime(0, &ts);  /* CLOCK_REALTIME = 0 */
    if (rc < 0) {
        if (tloc) *tloc = (time_t)-1;
        return (time_t)-1;
    }
    time_t t = (time_t)ts.tv_sec;
    if (tloc) *tloc = t;
    return t;
}

clock_t clock(void) {
    return (clock_t)_get_cpu_clock();
}

double difftime(time_t t1, time_t t0) {
    return (double)(t1 - t0);
}

struct tm *gmtime(const time_t *timep) {
    time_t t = *timep;
    int days, rem;
    int y;

    _tm_buf.tm_isdst = 0;

    days = (int)(t / 86400);
    rem = (int)(t % 86400);
    if (rem < 0) { rem += 86400; days--; }

    _tm_buf.tm_hour = rem / 3600;
    rem %= 3600;
    _tm_buf.tm_min = rem / 60;
    _tm_buf.tm_sec = rem % 60;

    /* Jan 1 1970 was a Thursday (wday=4) */
    _tm_buf.tm_wday = (days + 4) % 7;
    if (_tm_buf.tm_wday < 0) _tm_buf.tm_wday += 7;

    /* Find year */
    y = 1970;
    if (days >= 0) {
        for (;;) {
            int yd = is_leap_year(y) ? 366 : 365;
            if (days < yd) break;
            days -= yd;
            y++;
        }
    } else {
        do {
            y--;
            int yd = is_leap_year(y) ? 366 : 365;
            days += yd;
        } while (days < 0);
    }
    _tm_buf.tm_year = y - 1900;
    _tm_buf.tm_yday = days;

    /* Find month */
    int leap = is_leap_year(y);
    int m;
    for (m = 0; m < 11; m++) {
        if (days < days_in_month[leap][m]) break;
        days -= days_in_month[leap][m];
    }
    _tm_buf.tm_mon = m;
    _tm_buf.tm_mday = days + 1;

    return &_tm_buf;
}

struct tm *localtime(const time_t *timep) {
    /* No timezone support — localtime == gmtime */
    return gmtime(timep);
}

time_t mktime(struct tm *tm) {
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon;
    int d = tm->tm_mday;

    /* Normalize month */
    while (m < 0) { m += 12; y--; }
    while (m >= 12) { m -= 12; y++; }

    /* Count days from epoch */
    long days = 0;
    if (y >= 1970) {
        for (int i = 1970; i < y; i++)
            days += is_leap_year(i) ? 366 : 365;
    } else {
        for (int i = y; i < 1970; i++)
            days -= is_leap_year(i) ? 366 : 365;
    }

    int leap = is_leap_year(y);
    for (int i = 0; i < m; i++)
        days += days_in_month[leap][i];
    days += d - 1;

    time_t t = (time_t)days * 86400 + tm->tm_hour * 3600 +
               tm->tm_min * 60 + tm->tm_sec;

    /* Normalize back into tm */
    struct tm *normalized = gmtime(&t);
    *tm = *normalized;
    return t;
}

static const char *day_abbr[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char *day_full[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};
static const char *mon_abbr[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char *mon_full[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    char *p = s;
    char *end = s + max;
    char buf[64];

    while (*fmt && p < end - 1) {
        if (*fmt != '%') {
            *p++ = *fmt++;
            continue;
        }
        fmt++;  /* skip '%' */
        buf[0] = '\0';

        switch (*fmt) {
        case 'a': snprintf(buf, sizeof(buf), "%s", day_abbr[tm->tm_wday]); break;
        case 'A': snprintf(buf, sizeof(buf), "%s", day_full[tm->tm_wday]); break;
        case 'b': case 'h':
            snprintf(buf, sizeof(buf), "%s", mon_abbr[tm->tm_mon]); break;
        case 'B': snprintf(buf, sizeof(buf), "%s", mon_full[tm->tm_mon]); break;
        case 'c':
            snprintf(buf, sizeof(buf), "%s %s %2d %02d:%02d:%02d %d",
                     day_abbr[tm->tm_wday], mon_abbr[tm->tm_mon],
                     tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
                     tm->tm_year + 1900);
            break;
        case 'd': snprintf(buf, sizeof(buf), "%02d", tm->tm_mday); break;
        case 'H': snprintf(buf, sizeof(buf), "%02d", tm->tm_hour); break;
        case 'I': snprintf(buf, sizeof(buf), "%02d",
                    tm->tm_hour == 0 ? 12 : (tm->tm_hour > 12 ? tm->tm_hour - 12 : tm->tm_hour));
                  break;
        case 'j': snprintf(buf, sizeof(buf), "%03d", tm->tm_yday + 1); break;
        case 'm': snprintf(buf, sizeof(buf), "%02d", tm->tm_mon + 1); break;
        case 'M': snprintf(buf, sizeof(buf), "%02d", tm->tm_min); break;
        case 'p': snprintf(buf, sizeof(buf), "%s", tm->tm_hour < 12 ? "AM" : "PM"); break;
        case 'S': snprintf(buf, sizeof(buf), "%02d", tm->tm_sec); break;
        case 'U': {
            /* Week number (Sunday start) */
            int wnum = (tm->tm_yday + 7 - tm->tm_wday) / 7;
            snprintf(buf, sizeof(buf), "%02d", wnum);
            break;
        }
        case 'w': snprintf(buf, sizeof(buf), "%d", tm->tm_wday); break;
        case 'W': {
            /* Week number (Monday start) */
            int wday_mon = (tm->tm_wday + 6) % 7;
            int wnum = (tm->tm_yday + 7 - wday_mon) / 7;
            snprintf(buf, sizeof(buf), "%02d", wnum);
            break;
        }
        case 'x':
            snprintf(buf, sizeof(buf), "%02d/%02d/%02d",
                     tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100);
            break;
        case 'X':
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                     tm->tm_hour, tm->tm_min, tm->tm_sec);
            break;
        case 'y': snprintf(buf, sizeof(buf), "%02d", tm->tm_year % 100); break;
        case 'Y': snprintf(buf, sizeof(buf), "%d", tm->tm_year + 1900); break;
        case 'Z': snprintf(buf, sizeof(buf), "UTC"); break;
        case '%': buf[0] = '%'; buf[1] = '\0'; break;
        default:
            /* Unknown specifier — output literally */
            buf[0] = '%'; buf[1] = *fmt; buf[2] = '\0';
            break;
        }

        fmt++;
        size_t len = strlen(buf);
        if (p + len >= end) { *s = '\0'; return 0; }
        memcpy(p, buf, len);
        p += len;
    }

    *p = '\0';
    return (size_t)(p - s);
}

/* Stub locale functions needed by Lua's loslib.c */
static struct {
    char decimal_point[2];
} _lconv_data = { "." };

static struct lconv _lconv = {
    .decimal_point = _lconv_data.decimal_point
};

char *setlocale(int category, const char *locale) {
    (void)category;
    (void)locale;
    return "C";
}

struct lconv *localeconv(void) {
    return &_lconv;
}

/* Stub signal function — no signal support on bare metal */
#include <signal.h>

sighandler_t signal(int signum, sighandler_t handler) {
    (void)signum;
    (void)handler;
    return SIG_DFL;
}
