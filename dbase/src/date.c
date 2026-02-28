#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "date.h"
#include "set.h"

/* Standard astronomical Julian Day Number formula */
int32_t date_to_jdn(int year, int month, int day) {
    int a, y, m;
    a = (14 - month) / 12;
    y = year + 4800 - a;
    m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
}

void date_from_jdn(int32_t jdn, int *year, int *month, int *day) {
    int32_t a, b, c, d, e, m;
    a = jdn + 32044;
    b = (4 * a + 3) / 146097;
    c = a - (146097 * b) / 4;
    d = (4 * c + 3) / 1461;
    e = c - (1461 * d) / 4;
    m = (5 * e + 2) / 153;
    *day   = e - (153 * m + 2) / 5 + 1;
    *month = m + 3 - 12 * (m / 10);
    *year  = 100 * b + d - 4800 + m / 10;
}

int32_t date_from_dbf(const char *raw) {
    char tmp[5];
    int y, m, d;

    if (raw[0] == ' ' || raw[0] == '\0') return 0;

    /* YYYYMMDD */
    tmp[0] = raw[0]; tmp[1] = raw[1]; tmp[2] = raw[2]; tmp[3] = raw[3]; tmp[4] = '\0';
    y = atoi(tmp);
    tmp[0] = raw[4]; tmp[1] = raw[5]; tmp[2] = '\0';
    m = atoi(tmp);
    tmp[0] = raw[6]; tmp[1] = raw[7]; tmp[2] = '\0';
    d = atoi(tmp);

    if (y == 0 && m == 0 && d == 0) return 0;
    return date_to_jdn(y, m, d);
}

void date_to_dbf(int32_t jdn, char *buf) {
    int y, m, d;
    if (jdn == 0) {
        memcpy(buf, "        ", 8);
        buf[8] = '\0';
        return;
    }
    date_from_jdn(jdn, &y, &m, &d);
    /* Manual formatting to avoid snprintf overhead */
    buf[0] = '0' + (y / 1000) % 10;
    buf[1] = '0' + (y / 100) % 10;
    buf[2] = '0' + (y / 10) % 10;
    buf[3] = '0' + y % 10;
    buf[4] = '0' + m / 10;
    buf[5] = '0' + m % 10;
    buf[6] = '0' + d / 10;
    buf[7] = '0' + d % 10;
    buf[8] = '\0';
}

int32_t date_from_mdy(const char *s) {
    int mm = 0, dd = 0, yy = 0;
    const char *p = s;

    /* Skip leading { if present */
    if (*p == '{') p++;
    while (*p == ' ') p++;

    mm = atoi(p);
    while (*p && *p != '/') p++;
    if (*p == '/') p++;
    dd = atoi(p);
    while (*p && *p != '/' && *p != '}') p++;
    if (*p == '/') p++;
    yy = atoi(p);

    if (yy < 50) yy += 2000;
    else if (yy < 100) yy += 1900;

    if (mm == 0 && dd == 0 && yy == 0) return 0;
    return date_to_jdn(yy, mm, dd);
}

void date_to_mdy(int32_t jdn, char *buf) {
    int y, m, d;
    if (jdn == 0) {
        memcpy(buf, "  /  /  ", 8);
        buf[8] = '\0';
        return;
    }
    date_from_jdn(jdn, &y, &m, &d);
    buf[0] = '0' + m / 10;
    buf[1] = '0' + m % 10;
    buf[2] = '/';
    buf[3] = '0' + d / 10;
    buf[4] = '0' + d % 10;
    buf[5] = '/';
    buf[6] = '0' + (y / 10) % 10;
    buf[7] = '0' + y % 10;
    buf[8] = '\0';
}

/* ---- Format-aware date display ---- */
static void fmt_2digit(char *p, int val) {
    p[0] = '0' + (val / 10) % 10;
    p[1] = '0' + val % 10;
}

void date_to_display(int32_t jdn, char *buf, date_format_t fmt, int century, char mark) {
    int y, m, d;
    char sep;

    if (jdn == 0) {
        /* Blank date: use appropriate separator and width */
        int w = century ? 10 : 8;
        int i;
        for (i = 0; i < w; i++) buf[i] = ' ';
        buf[w] = '\0';
        return;
    }

    date_from_jdn(jdn, &y, &m, &d);

    /* Determine separator: mark overrides format default */
    switch (fmt) {
    case DATE_AMERICAN: sep = '/'; break;
    case DATE_ANSI:     sep = '.'; break;
    case DATE_BRITISH:  sep = '/'; break;
    case DATE_FRENCH:   sep = '/'; break;
    case DATE_GERMAN:   sep = '.'; break;
    case DATE_ITALIAN:  sep = '-'; break;
    case DATE_JAPAN:    sep = '/'; break;
    default:            sep = '/'; break;
    }
    if (mark != '\0') sep = mark;

    switch (fmt) {
    case DATE_AMERICAN: /* MM/DD/YY or MM/DD/YYYY */
        fmt_2digit(buf, m); buf[2] = sep;
        fmt_2digit(buf + 3, d); buf[5] = sep;
        if (century) {
            fmt_2digit(buf + 6, y / 100);
            fmt_2digit(buf + 8, y % 100);
            buf[10] = '\0';
        } else {
            fmt_2digit(buf + 6, y % 100);
            buf[8] = '\0';
        }
        break;
    case DATE_ANSI: /* YY.MM.DD or YYYY.MM.DD */
    case DATE_JAPAN: /* YY/MM/DD or YYYY/MM/DD */
        if (century) {
            fmt_2digit(buf, y / 100);
            fmt_2digit(buf + 2, y % 100);
            buf[4] = sep;
            fmt_2digit(buf + 5, m); buf[7] = sep;
            fmt_2digit(buf + 8, d);
            buf[10] = '\0';
        } else {
            fmt_2digit(buf, y % 100);
            buf[2] = sep;
            fmt_2digit(buf + 3, m); buf[5] = sep;
            fmt_2digit(buf + 6, d);
            buf[8] = '\0';
        }
        break;
    case DATE_BRITISH: /* DD/MM/YY or DD/MM/YYYY */
    case DATE_FRENCH:
    case DATE_GERMAN: /* DD.MM.YY or DD.MM.YYYY */
    case DATE_ITALIAN: /* DD-MM-YY or DD-MM-YYYY */
        fmt_2digit(buf, d); buf[2] = sep;
        fmt_2digit(buf + 3, m); buf[5] = sep;
        if (century) {
            fmt_2digit(buf + 6, y / 100);
            fmt_2digit(buf + 8, y % 100);
            buf[10] = '\0';
        } else {
            fmt_2digit(buf + 6, y % 100);
            buf[8] = '\0';
        }
        break;
    }
}

/* Parse a number from string, advancing pointer past digits */
static int parse_num(const char **pp) {
    int n = 0;
    while (**pp >= '0' && **pp <= '9') {
        n = n * 10 + (**pp - '0');
        (*pp)++;
    }
    return n;
}

/* Skip one separator character (any of / . -) */
static void skip_sep(const char **pp) {
    if (**pp == '/' || **pp == '.' || **pp == '-') (*pp)++;
}

int32_t date_from_display(const char *s, date_format_t fmt, int epoch) {
    const char *p = s;
    int a, b, c;
    int yy, mm, dd;

    /* Skip leading { if present */
    if (*p == '{') p++;
    while (*p == ' ') p++;

    /* Parse three numeric components separated by / . or - */
    a = parse_num(&p); skip_sep(&p);
    b = parse_num(&p); skip_sep(&p);
    c = parse_num(&p);

    /* Assign to yy/mm/dd based on format */
    switch (fmt) {
    case DATE_AMERICAN:             /* MM/DD/YY */
        mm = a; dd = b; yy = c; break;
    case DATE_ANSI:                 /* YY.MM.DD */
    case DATE_JAPAN:                /* YY/MM/DD */
        yy = a; mm = b; dd = c; break;
    case DATE_BRITISH:              /* DD/MM/YY */
    case DATE_FRENCH:
    case DATE_GERMAN:               /* DD.MM.YY */
    case DATE_ITALIAN:              /* DD-MM-YY */
        dd = a; mm = b; yy = c; break;
    default:
        mm = a; dd = b; yy = c; break;
    }

    /* 2-digit year windowing */
    if (yy >= 0 && yy < 100) {
        if (epoch > 0) {
            int base = (epoch / 100) * 100;
            int pivot = epoch % 100;
            yy = (yy < pivot) ? base + 100 + yy : base + yy;
        } else {
            if (yy < 50) yy += 2000;
            else yy += 1900;
        }
    }

    if (mm == 0 && dd == 0 && yy == 0) return 0;
    return date_to_jdn(yy, mm, dd);
}

int date_dow(int32_t jdn) {
    /* JDN mod 7: 0=Mon, 1=Tue, ..., 6=Sun. We want 1=Sun..7=Sat. */
    int d = (jdn + 1) % 7; /* 0=Sun..6=Sat */
    return d + 1;           /* 1=Sun..7=Sat */
}

static const char *dow_names[] = {
    "", "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

static const char *month_names[] = {
    "", "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

const char *date_dow_name(int dow) {
    if (dow < 1 || dow > 7) return "";
    return dow_names[dow];
}

const char *date_month_name(int month) {
    if (month < 1 || month > 12) return "";
    return month_names[month];
}

int date_days_in_month(int year, int month) {
    static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2) {
        if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
            return 29;
    }
    return days[month];
}

int32_t date_today(void) {
    time_t t;
    struct tm *tm;
    t = time(NULL);
    tm = localtime(&t);
    if (!tm) return 0;
    return date_to_jdn(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
}
