#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "field.h"
#include "date.h"
#include "command.h"

int field_format_char(char *buf, int field_len, const char *value) {
    int i, vlen;
    vlen = strlen(value);
    if (vlen > field_len) vlen = field_len;
    for (i = 0; i < vlen; i++)
        buf[i] = value[i];
    for (; i < field_len; i++)
        buf[i] = ' ';
    buf[field_len] = '\0';
    return 0;
}

int field_format_numeric(char *buf, int field_len, int decimals, const char *value) {
    double v;
    int len;
    char tmp[64];

    v = atof(value);
    if (decimals > 0)
        snprintf(tmp, sizeof(tmp), "%*.*f", field_len, decimals, v);
    else
        snprintf(tmp, sizeof(tmp), "%*d", field_len, (int)v);

    len = strlen(tmp);
    if (len > field_len) {
        /* Overflow: fill with asterisks */
        int i;
        for (i = 0; i < field_len; i++)
            buf[i] = '*';
        buf[field_len] = '\0';
        return 0;
    }
    /* Right-justify, space pad */
    if (len < field_len) {
        int pad = field_len - len;
        int i;
        for (i = 0; i < pad; i++)
            buf[i] = ' ';
        memcpy(buf + pad, tmp, len);
    } else {
        memcpy(buf, tmp, len);
    }
    buf[field_len] = '\0';
    return 0;
}

int field_format_date(char *buf, const char *value) {
    /* Input: date string in current SET DATE format, Output: YYYYMMDD (8 bytes) */
    int32_t jdn = date_from_display(value, cmd_get_date_format(), cmd_get_epoch());
    date_to_dbf(jdn, buf);
    return 0;
}

int field_format_logical(char *buf, const char *value) {
    const char *p = value;
    while (*p == ' ' || *p == '.') p++;
    if (*p == 'T' || *p == 't' || *p == 'Y' || *p == 'y')
        buf[0] = 'T';
    else
        buf[0] = 'F';
    buf[1] = '\0';
    return 0;
}

void field_display_char(char *buf, const char *raw, int field_len) {
    memcpy(buf, raw, field_len);
    buf[field_len] = '\0';
}

void field_display_numeric(char *buf, const char *raw, int field_len) {
    memcpy(buf, raw, field_len);
    buf[field_len] = '\0';
}

void field_display_date(char *buf, const char *raw) {
    /* Input: YYYYMMDD, Output: format-aware date string */
    int32_t jdn = date_from_dbf(raw);
    date_to_display(jdn, buf, cmd_get_date_format(), cmd_get_century(), cmd_get_mark());
}

void field_display_logical(char *buf, const char *raw) {
    if (raw[0] == 'T' || raw[0] == 't')
        strcpy(buf, ".T.");
    else
        strcpy(buf, ".F.");
}
