#ifndef DATE_H
#define DATE_H

#include <stdint.h>

/* Julian Day Number conversions */
int32_t date_to_jdn(int year, int month, int day);
void    date_from_jdn(int32_t jdn, int *year, int *month, int *day);

/* DBF format: "YYYYMMDD" */
int32_t date_from_dbf(const char *raw);
void    date_to_dbf(int32_t jdn, char *buf);

/* Display format: "MM/DD/YY" (backward compat wrappers) */
int32_t date_from_mdy(const char *s);
void    date_to_mdy(int32_t jdn, char *buf);

/* Format-aware display: uses SET DATE and SET CENTURY settings */
#include "set.h"
void    date_to_display(int32_t jdn, char *buf, date_format_t fmt, int century, char mark);
int32_t date_from_display(const char *s, date_format_t fmt, int epoch);

/* Day of week: 1=Sun..7=Sat */
int     date_dow(int32_t jdn);
const char *date_dow_name(int dow);
const char *date_month_name(int month);

/* Days in a given month (handles leap years) */
int     date_days_in_month(int year, int month);

/* Current date as JDN */
int32_t date_today(void);

#endif
