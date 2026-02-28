#ifndef FIELD_H
#define FIELD_H

#include <stdint.h>

/*
 * Format a value for storage in a DBF field.
 * buf must be at least field_len+1 bytes.
 * Returns 0 on success, -1 on error.
 */
int field_format_char(char *buf, int field_len, const char *value);
int field_format_numeric(char *buf, int field_len, int decimals, const char *value);
int field_format_date(char *buf, const char *value);      /* MM/DD/YY -> YYYYMMDD */
int field_format_logical(char *buf, const char *value);    /* .T./.F./T/F -> T/F */

/*
 * Format a stored field value for display.
 * buf must be large enough (field_len+8 for safety).
 */
void field_display_char(char *buf, const char *raw, int field_len);
void field_display_numeric(char *buf, const char *raw, int field_len);
void field_display_date(char *buf, const char *raw);       /* YYYYMMDD -> MM/DD/YY */
void field_display_logical(char *buf, const char *raw);    /* T/F -> .T./.F. */

#endif
