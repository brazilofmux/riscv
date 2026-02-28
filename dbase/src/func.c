#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include "func.h"
#include "date.h"
#include "command.h"
#include "screen.h"
#include "program.h"
#include "util.h"
#include "menu.h"
#include "area.h"
#include "index.h"

extern double floor(double x);
extern double fmod(double x, double y);

/* ---- Low-level File I/O (Clipper/FoxPro) ---- */
#define MAX_LL_FILES 16
static FILE *ll_files[MAX_LL_FILES];
static int ll_error = 0;

static int fn_fcreate(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "FCREATE requires (filename)";
        return -1;
    }
    for (int i = 0; i < MAX_LL_FILES; i++) {
        if (!ll_files[i]) {
            ll_files[i] = fopen(args[0].str, "wb+");
            if (ll_files[i]) {
                ll_error = 0;
                *result = val_num((double)i + 1);
            } else {
                ll_error = errno;
                *result = val_num(-1);
            }
            return 0;
        }
    }
    ll_error = -1;
    *result = val_num(-1);
    return 0;
}

static int fn_fopen(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "FOPEN requires (filename [, mode])";
        return -1;
    }
    const char *mode = "rb+";
    if (nargs >= 2 && args[1].type == VAL_NUM) {
        int m = (int)args[1].num;
        if (m == 0) mode = "rb";
        else if (m == 1) mode = "wb";
        else if (m == 2) mode = "rb+";
    }
    for (int i = 0; i < MAX_LL_FILES; i++) {
        if (!ll_files[i]) {
            ll_files[i] = fopen(args[0].str, mode);
            if (ll_files[i]) {
                ll_error = 0;
                *result = val_num((double)i + 1);
            } else {
                ll_error = errno;
                *result = val_num(-1);
            }
            return 0;
        }
    }
    ll_error = -1;
    *result = val_num(-1);
    return 0;
}

static int fn_fclose(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_NUM) {
        ctx->error = "FCLOSE requires (handle)";
        return -1;
    }
    int h = (int)args[0].num - 1;
    if (h >= 0 && h < MAX_LL_FILES && ll_files[h]) {
        fclose(ll_files[h]);
        ll_files[h] = NULL;
        ll_error = 0;
        *result = val_logic(1);
    } else {
        *result = val_logic(0);
    }
    return 0;
}

static int fn_fread(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_NUM || args[1].type != VAL_NUM) {
        ctx->error = "FREAD requires (handle, count)";
        return -1;
    }
    int h = (int)args[0].num - 1;
    int count = (int)args[1].num;
    if (count > 255) count = 255;
    if (h >= 0 && h < MAX_LL_FILES && ll_files[h] && count > 0) {
        char *buf = (char *)malloc(count + 1);
        if (!buf) { *result = val_str(""); return 0; }
        size_t n = fread(buf, 1, count, ll_files[h]);
        buf[n] = '\0';
        *result = val_str(buf);
        free(buf);
        ll_error = ferror(ll_files[h]) ? 1 : 0;
    } else {
        *result = val_str("");
    }
    return 0;
}

static int fn_fwrite(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_NUM || args[1].type != VAL_CHAR) {
        ctx->error = "FWRITE requires (handle, string [, count])";
        return -1;
    }
    int h = (int)args[0].num - 1;
    int count = (int)strlen(args[1].str);
    if (nargs >= 3 && args[2].type == VAL_NUM) {
        int req = (int)args[2].num;
        if (req < count) count = req;
    }
    if (count < 0) count = 0;
    if (h >= 0 && h < MAX_LL_FILES && ll_files[h]) {
        size_t n = fwrite(args[1].str, 1, count, ll_files[h]);
        *result = val_num((double)n);
        ll_error = (n < (size_t)count) ? 1 : 0;
    } else {
        *result = val_num(0);
    }
    return 0;
}

static int fn_fseek(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_NUM || args[1].type != VAL_NUM) {
        ctx->error = "FSEEK requires (handle, offset [, origin])";
        return -1;
    }
    int h = (int)args[0].num - 1;
    long offset = (long)args[1].num;
    int origin = SEEK_SET;
    if (nargs >= 3 && args[2].type == VAL_NUM) {
        int o = (int)args[2].num;
        if (o == 1) origin = SEEK_CUR;
        else if (o == 2) origin = SEEK_END;
    }
    if (h >= 0 && h < MAX_LL_FILES && ll_files[h]) {
        fseek(ll_files[h], offset, origin);
        *result = val_num((double)ftell(ll_files[h]));
        ll_error = 0;
    } else {
        *result = val_num(-1);
    }
    return 0;
}

static int fn_ferror(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num((double)ll_error);
    return 0;
}

void ll_close_all(void) {
    for (int i = 0; i < MAX_LL_FILES; i++) {
        if (ll_files[i]) {
            fclose(ll_files[i]);
            ll_files[i] = NULL;
        }
    }
}

/* ---- Inline sqrt via Newton's method (hardware sqrt HALTs on slow32) ---- */
static double my_sqrt(double x) {
    double guess, prev;
    int i;
    if (x <= 0.0) return 0.0;
    guess = x;
    /* Rough initial guess */
    if (x > 1.0) guess = x / 2.0;
    for (i = 0; i < 30; i++) {
        prev = guess;
        guess = (guess + x / guess) / 2.0;
        /* Check convergence */
        if (guess == prev) break;
    }
    return guess;
}

static double my_fabs(double x) {
    return x < 0.0 ? -x : x;
}

/* ---- Status functions ---- */

static int fn_eof(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)args; (void)nargs;
    if (!ctx->db || !dbf_is_open(ctx->db))
        *result = val_logic(1);
    else
        *result = val_logic(ctx->eof_flag);
    return 0;
}

static int fn_bof(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)args; (void)nargs;
    *result = val_logic(ctx->bof_flag);
    return 0;
}

static int fn_recno(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)args; (void)nargs;
    if (!ctx->db || !dbf_is_open(ctx->db))
        *result = val_num(0);
    else
        *result = val_num((double)ctx->db->current_record);
    return 0;
}

static int fn_reccount(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)args; (void)nargs;
    if (!ctx->db || !dbf_is_open(ctx->db))
        *result = val_num(0);
    else
        *result = val_num((double)ctx->db->record_count);
    return 0;
}

static int fn_deleted(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)args; (void)nargs;
    if (!ctx->db || !dbf_is_open(ctx->db) || ctx->db->current_record == 0)
        *result = val_logic(0);
    else
        *result = val_logic(ctx->db->record_buf[0] == '*');
    return 0;
}

static int fn_found(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)args; (void)nargs;
    *result = val_logic(ctx->found);
    return 0;
}

/* ---- String functions ---- */

static int fn_substr(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    int start, len, slen;
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_CHAR || args[1].type != VAL_NUM) {
        ctx->error = "SUBSTR requires (string, start [, length])";
        return -1;
    }
    slen = strlen(args[0].str);
    start = (int)args[1].num - 1; /* 1-based to 0-based */
    if (start < 0) start = 0;
    if (start >= slen) { *result = val_str(""); return 0; }

    len = slen - start;
    if (nargs >= 3 && args[2].type == VAL_NUM) {
        int req = (int)args[2].num;
        if (req < len) len = req;
    }
    if (len < 0) len = 0;

    {
        char buf[256];
        if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
        memcpy(buf, args[0].str + start, len);
        buf[len] = '\0';
        *result = val_str(buf);
    }
    return 0;
}

static int fn_trim(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    int len;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "TRIM requires (string)";
        return -1;
    }
    str_copy(buf, args[0].str, sizeof(buf));
    len = strlen(buf);
    while (len > 0 && buf[len-1] == ' ') len--;
    buf[len] = '\0';
    *result = val_str(buf);
    return 0;
}

static int fn_ltrim(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    const char *p;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "LTRIM requires (string)";
        return -1;
    }
    p = args[0].str;
    while (*p == ' ') p++;
    *result = val_str(p);
    return 0;
}

static int fn_alltrim(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    int len;
    const char *p;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "ALLTRIM requires (string)";
        return -1;
    }
    p = args[0].str;
    while (*p == ' ') p++;
    str_copy(buf, p, sizeof(buf));
    len = strlen(buf);
    while (len > 0 && buf[len - 1] == ' ') len--;
    buf[len] = '\0';
    *result = val_str(buf);
    return 0;
}

static int fn_upper(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "UPPER requires (string)";
        return -1;
    }
    str_copy(buf, args[0].str, sizeof(buf));
    str_upper(buf);
    *result = val_str(buf);
    return 0;
}

static int fn_lower(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    int i;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "LOWER requires (string)";
        return -1;
    }
    str_copy(buf, args[0].str, sizeof(buf));
    for (i = 0; buf[i]; i++)
        if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] += 32;
    *result = val_str(buf);
    return 0;
}

static int fn_at(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    const char *p;
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_CHAR || args[1].type != VAL_CHAR) {
        ctx->error = "AT requires (search, string)";
        return -1;
    }
    p = strstr(args[1].str, args[0].str);
    *result = val_num(p ? (double)(p - args[1].str + 1) : 0.0);
    return 0;
}

static int fn_len(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "LEN requires (string)";
        return -1;
    }
    *result = val_num((double)strlen(args[0].str));
    return 0;
}

static int fn_space(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    int n, i;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_NUM) {
        ctx->error = "SPACE requires (number)";
        return -1;
    }
    n = (int)args[0].num;
    if (n < 0) n = 0;
    if (n > 255) n = 255;
    for (i = 0; i < n; i++) buf[i] = ' ';
    buf[n] = '\0';
    *result = val_str(buf);
    return 0;
}

static int fn_replicate(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    int n, slen, i, pos;
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_CHAR || args[1].type != VAL_NUM) {
        ctx->error = "REPLICATE requires (string, count)";
        return -1;
    }
    n = (int)args[1].num;
    slen = strlen(args[0].str);
    pos = 0;
    for (i = 0; i < n && pos + slen < (int)sizeof(buf); i++) {
        memcpy(buf + pos, args[0].str, slen);
        pos += slen;
    }
    buf[pos] = '\0';
    *result = val_str(buf);
    return 0;
}

static int fn_padr(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    int width, slen, i;
    char pad = ' ';
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_CHAR || args[1].type != VAL_NUM) {
        ctx->error = "PADR requires (string, width [, char])";
        return -1;
    }
    width = (int)args[1].num;
    if (width < 0) width = 0;
    if (width > (int)sizeof(buf) - 1) width = (int)sizeof(buf) - 1;
    if (nargs >= 3 && args[2].type == VAL_CHAR && args[2].str[0]) pad = args[2].str[0];

    slen = strlen(args[0].str);
    if (slen >= width) {
        memcpy(buf, args[0].str, width);
        buf[width] = '\0';
    } else {
        memcpy(buf, args[0].str, slen);
        for (i = slen; i < width; i++) buf[i] = pad;
        buf[width] = '\0';
    }
    *result = val_str(buf);
    return 0;
}

static int fn_padl(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    int width, slen, i, padlen;
    char pad = ' ';
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_CHAR || args[1].type != VAL_NUM) {
        ctx->error = "PADL requires (string, width [, char])";
        return -1;
    }
    width = (int)args[1].num;
    if (width < 0) width = 0;
    if (width > (int)sizeof(buf) - 1) width = (int)sizeof(buf) - 1;
    if (nargs >= 3 && args[2].type == VAL_CHAR && args[2].str[0]) pad = args[2].str[0];

    slen = strlen(args[0].str);
    if (slen >= width) {
        memcpy(buf, args[0].str + (slen - width), width);
        buf[width] = '\0';
    } else {
        padlen = width - slen;
        for (i = 0; i < padlen; i++) buf[i] = pad;
        memcpy(buf + padlen, args[0].str, slen);
        buf[width] = '\0';
    }
    *result = val_str(buf);
    return 0;
}

static int fn_padc(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    int width, slen, i, lpad, rpad;
    char pad = ' ';
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_CHAR || args[1].type != VAL_NUM) {
        ctx->error = "PADC requires (string, width [, char])";
        return -1;
    }
    width = (int)args[1].num;
    if (width < 0) width = 0;
    if (width > (int)sizeof(buf) - 1) width = (int)sizeof(buf) - 1;
    if (nargs >= 3 && args[2].type == VAL_CHAR && args[2].str[0]) pad = args[2].str[0];

    slen = strlen(args[0].str);
    if (slen >= width) {
        lpad = (slen - width) / 2;
        memcpy(buf, args[0].str + lpad, width);
        buf[width] = '\0';
    } else {
        lpad = (width - slen) / 2;
        rpad = width - slen - lpad;
        for (i = 0; i < lpad; i++) buf[i] = pad;
        memcpy(buf + lpad, args[0].str, slen);
        for (i = 0; i < rpad; i++) buf[lpad + slen + i] = pad;
        buf[width] = '\0';
    }
    *result = val_str(buf);
    return 0;
}

static int fn_left(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    int n, slen;
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_CHAR || args[1].type != VAL_NUM) {
        ctx->error = "LEFT requires (string, count)";
        return -1;
    }
    n = (int)args[1].num;
    slen = strlen(args[0].str);
    if (n > slen) n = slen;
    if (n < 0) n = 0;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    memcpy(buf, args[0].str, n);
    buf[n] = '\0';
    *result = val_str(buf);
    return 0;
}

static int fn_right(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    int n, slen, start;
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_CHAR || args[1].type != VAL_NUM) {
        ctx->error = "RIGHT requires (string, count)";
        return -1;
    }
    n = (int)args[1].num;
    slen = strlen(args[0].str);
    if (n > slen) n = slen;
    if (n < 0) n = 0;
    start = slen - n;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    memcpy(buf, args[0].str + start, n);
    buf[n] = '\0';
    *result = val_str(buf);
    return 0;
}

static int fn_str(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[64];
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_NUM) {
        ctx->error = "STR requires (number [, width [, decimals]])";
        return -1;
    }
    {
        int width = 10;
        int dec = 0;
        if (nargs >= 2 && args[1].type == VAL_NUM) width = (int)args[1].num;
        if (nargs >= 3 && args[2].type == VAL_NUM) dec = (int)args[2].num;
        if (width > (int)sizeof(buf) - 1) width = (int)sizeof(buf) - 1;
        if (dec > 0)
            snprintf(buf, sizeof(buf), "%*.*f", width, dec, args[0].num);
        else {
            double n = args[0].num;
            int rounded = (n >= 0.0) ? (int)(n + 0.5) : (int)(n - 0.5);
            snprintf(buf, sizeof(buf), "%*d", width, rounded);
        }
    }
    *result = val_str(buf);
    return 0;
}

static int fn_val(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "VAL requires (string)";
        return -1;
    }
    *result = val_num(atof(args[0].str));
    return 0;
}

static int fn_chr(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[2];
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_NUM) {
        ctx->error = "CHR requires (number)";
        return -1;
    }
    buf[0] = (char)(int)args[0].num;
    buf[1] = '\0';
    *result = val_str(buf);
    return 0;
}

static int fn_asc(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR || args[0].str[0] == '\0') {
        ctx->error = "ASC requires (string)";
        return -1;
    }
    *result = val_num((double)(unsigned char)args[0].str[0]);
    return 0;
}

static int fn_type(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1) {
        *result = val_str("U");
        return 0;
    }
    switch (args[0].type) {
    case VAL_NUM:   *result = val_str("N"); break;
    case VAL_CHAR:  *result = val_str("C"); break;
    case VAL_DATE:  *result = val_str("D"); break;
    case VAL_LOGIC: *result = val_str("L"); break;
    default:        *result = val_str("U"); break;
    }
    return 0;
}

/* ---- Numeric functions ---- */

static int fn_int(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_NUM) {
        ctx->error = "INT requires (number)";
        return -1;
    }
    *result = val_num((double)(int)args[0].num);
    return 0;
}

static int fn_round(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    double n, p;
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_NUM || args[1].type != VAL_NUM) {
        ctx->error = "ROUND requires (number, decimals)";
        return -1;
    }
    n = args[0].num;
    {
        int dec = (int)args[1].num;
        int i;
        p = 1.0;
        for (i = 0; i < dec; i++) p *= 10.0;
        *result = val_num(floor(n * p + 0.5) / p);
    }
    return 0;
}

static int fn_abs(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_NUM) {
        ctx->error = "ABS requires (number)";
        return -1;
    }
    *result = val_num(my_fabs(args[0].num));
    return 0;
}

static int fn_mod(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_NUM || args[1].type != VAL_NUM) {
        ctx->error = "MOD requires (number, divisor)";
        return -1;
    }
    if (args[1].num == 0.0) {
        ctx->error = "Division by zero in MOD";
        return -1;
    }
    *result = val_num(fmod(args[0].num, args[1].num));
    return 0;
}

static int fn_sqrt(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_NUM) {
        ctx->error = "SQRT requires (number)";
        return -1;
    }
    *result = val_num(my_sqrt(args[0].num));
    return 0;
}

static int fn_max(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_NUM || args[1].type != VAL_NUM) {
        ctx->error = "MAX requires (number, number)";
        return -1;
    }
    *result = val_num(args[0].num > args[1].num ? args[0].num : args[1].num);
    return 0;
}

static int fn_min(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_NUM || args[1].type != VAL_NUM) {
        ctx->error = "MIN requires (number, number)";
        return -1;
    }
    *result = val_num(args[0].num < args[1].num ? args[0].num : args[1].num);
    return 0;
}

/* ---- Date functions ---- */

static int fn_date(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_date(date_today());
    return 0;
}

static int fn_dtoc(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[16];
    if (nargs < 1 || args[0].type != VAL_DATE) {
        ctx->error = "DTOC requires (date)";
        return -1;
    }
    date_to_display(args[0].date, buf, cmd_get_date_format(), cmd_get_century(), cmd_get_mark());
    *result = val_str(buf);
    return 0;
}

static int fn_ctod(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "CTOD requires (string)";
        return -1;
    }
    *result = val_date(date_from_display(args[0].str, cmd_get_date_format(), cmd_get_epoch()));
    return 0;
}

static int fn_day(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    int y, m, d;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_DATE) {
        ctx->error = "DAY requires (date)";
        return -1;
    }
    date_from_jdn(args[0].date, &y, &m, &d);
    *result = val_num((double)d);
    return 0;
}

static int fn_month(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    int y, m, d;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_DATE) {
        ctx->error = "MONTH requires (date)";
        return -1;
    }
    date_from_jdn(args[0].date, &y, &m, &d);
    *result = val_num((double)m);
    return 0;
}

static int fn_year(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    int y, m, d;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_DATE) {
        ctx->error = "YEAR requires (date)";
        return -1;
    }
    date_from_jdn(args[0].date, &y, &m, &d);
    *result = val_num((double)y);
    return 0;
}

static int fn_dow(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_DATE) {
        ctx->error = "DOW requires (date)";
        return -1;
    }
    *result = val_num((double)date_dow(args[0].date));
    return 0;
}

static int fn_cdow(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_DATE) {
        ctx->error = "CDOW requires (date)";
        return -1;
    }
    *result = val_str(date_dow_name(date_dow(args[0].date)));
    return 0;
}

static int fn_cmonth(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    int y, m, d;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_DATE) {
        ctx->error = "CMONTH requires (date)";
        return -1;
    }
    date_from_jdn(args[0].date, &y, &m, &d);
    *result = val_str(date_month_name(m));
    return 0;
}

static int fn_empty(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1) {
        *result = val_logic(1);
        return 0;
    }
    switch (args[0].type) {
    case VAL_CHAR: {
        const char *p = args[0].str;
        while (*p == ' ') p++;
        *result = val_logic(*p == '\0');
        break;
    }
    case VAL_NUM:
        *result = val_logic(args[0].num == 0.0);
        break;
    case VAL_LOGIC:
        *result = val_logic(args[0].logic == 0);
        break;
    case VAL_DATE:
        *result = val_logic(args[0].date == 0);
        break;
    default:
        *result = val_logic(1);
        break;
    }
    return 0;
}

/* ---- IIF (inline if) ---- */

static int fn_iif(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 3) {
        ctx->error = "IIF requires (cond, true_val, false_val)";
        return -1;
    }
    /* Evaluate condition: numeric non-zero or logical true */
    int cond = 0;
    if (args[0].type == VAL_LOGIC) cond = args[0].logic;
    else if (args[0].type == VAL_NUM) cond = (args[0].num != 0.0);
    *result = cond ? args[1] : args[2];
    return 0;
}

/* ---- FILE (test file existence) ---- */

static int fn_file(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "FILE requires (filename)";
        return -1;
    }
    {
        FILE *f = fopen(args[0].str, "r");
        if (!f) {
            char up[256];
            str_copy(up, args[0].str, sizeof(up));
            str_upper(up);
            f = fopen(up, "r");
        }
        if (f) {
            fclose(f);
            *result = val_logic(1);
        } else {
            *result = val_logic(0);
        }
    }
    return 0;
}

/* ---- DTOS (date to string YYYYMMDD for indexing) ---- */

static int fn_dtos(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    int y, m, d;
    char buf[16];
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_DATE) {
        ctx->error = "DTOS requires (date)";
        return -1;
    }
    date_from_jdn(args[0].date, &y, &m, &d);
    buf[0] = '0' + (y / 1000) % 10;
    buf[1] = '0' + (y / 100) % 10;
    buf[2] = '0' + (y / 10) % 10;
    buf[3] = '0' + y % 10;
    buf[4] = '0' + (m / 10);
    buf[5] = '0' + (m % 10);
    buf[6] = '0' + (d / 10);
    buf[7] = '0' + (d % 10);
    buf[8] = '\0';
    *result = val_str(buf);
    return 0;
}

/* ---- Screen position ---- */

static int fn_row(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num((double)screen_get_row());
    return 0;
}

static int fn_col(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num((double)screen_get_col());
    return 0;
}

static int fn_prow(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num((double)screen_get_prow());
    return 0;
}

static int fn_pcol(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num((double)screen_get_pcol());
    return 0;
}

/* ---- BAR() / PROMPT() — popup menu result ---- */
static int fn_bar(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num((double)menu_last_bar());
    return 0;
}

static int fn_prompt_func(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_str(menu_last_prompt());
    return 0;
}

static int fn_pad(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_str(menu_last_pad());
    return 0;
}

static int fn_popup_func(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_str(menu_last_popup());
    return 0;
}

/* ---- STUFF(str, start, delete, insert) ---- */
static int fn_stuff(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[512];
    const char *s;
    int start, del, slen, ilen;
    (void)ctx;
    if (nargs < 4 || args[0].type != VAL_CHAR || args[3].type != VAL_CHAR) {
        *result = val_str("");
        return 0;
    }
    s = args[0].str;
    start = (int)args[1].num;
    del = (int)args[2].num;
    slen = strlen(s);
    ilen = strlen(args[3].str);
    if (start < 1) start = 1;
    if (start > slen + 1) start = slen + 1;
    if (del < 0) del = 0;
    if (start - 1 + del > slen) del = slen - (start - 1);
    /* Copy prefix */
    if (start - 1 > 0 && start - 1 < (int)sizeof(buf))
        memcpy(buf, s, start - 1);
    /* Copy insert string */
    if (start - 1 + ilen < (int)sizeof(buf))
        memcpy(buf + start - 1, args[3].str, ilen);
    /* Copy suffix */
    {
        int suffix_start = start - 1 + del;
        int suffix_len = slen - suffix_start;
        if (suffix_len > 0 && start - 1 + ilen + suffix_len < (int)sizeof(buf))
            memcpy(buf + start - 1 + ilen, s + suffix_start, suffix_len);
        buf[start - 1 + ilen + (suffix_len > 0 ? suffix_len : 0)] = '\0';
    }
    *result = val_str(buf);
    return 0;
}

/* ---- TRANSFORM(expr, picture) ---- */
static int fn_transform(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256], formatted[256];
    (void)ctx;
    if (nargs < 2) { *result = val_str(""); return 0; }
    val_to_string(&args[0], buf, sizeof(buf));
    /* Reuse apply_picture logic inline */
    {
        const char *value = buf;
        const char *picture = (args[1].type == VAL_CHAR) ? args[1].str : "";
        int plen = strlen(picture);
        int vlen = strlen(value);
        int vi = 0, i;
        if (plen == 0) {
            *result = val_str(buf);
            return 0;
        }
        for (i = 0; i < plen && i < (int)sizeof(formatted) - 1; i++) {
            char pc = picture[i];
            char vc = (vi < vlen) ? value[vi] : ' ';
            switch (pc) {
            case '!': if (vc >= 'a' && vc <= 'z') vc -= 32; formatted[i] = vc; vi++; break;
            case '9': formatted[i] = (vc >= '0' && vc <= '9') ? vc : ' '; vi++; break;
            case 'A': formatted[i] = ((vc >= 'A' && vc <= 'Z') || (vc >= 'a' && vc <= 'z')) ? vc : ' '; vi++; break;
            case 'X': formatted[i] = vc; vi++; break;
            case '#': formatted[i] = (vc >= '0' && vc <= '9' || vc == ' ' || vc == '+' || vc == '-') ? vc : ' '; vi++; break;
            default: formatted[i] = pc; break;
            }
        }
        formatted[i] = '\0';
    }
    *result = val_str(formatted);
    return 0;
}

/* ---- ISALPHA / ISUPPER / ISLOWER ---- */
static int fn_isalpha(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR || args[0].str[0] == '\0') {
        *result = val_logic(0); return 0;
    }
    { char c = args[0].str[0];
      *result = val_logic((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')); }
    return 0;
}

static int fn_isupper(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR || args[0].str[0] == '\0') {
        *result = val_logic(0); return 0;
    }
    { char c = args[0].str[0]; *result = val_logic(c >= 'A' && c <= 'Z'); }
    return 0;
}

static int fn_islower(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR || args[0].str[0] == '\0') {
        *result = val_logic(0); return 0;
    }
    { char c = args[0].str[0]; *result = val_logic(c >= 'a' && c <= 'z'); }
    return 0;
}

/* ---- VERSION / OS ---- */
static int fn_curdir(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    (void)ctx; (void)args; (void)nargs;
    if (getcwd(buf, sizeof(buf))) {
        *result = val_str(buf);
    } else {
        *result = val_str("");
    }
    return 0;
}

static int fn_adir(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    int count = 0;
    DIR *dir;
    struct dirent *ent;
    const char *path = ".";
    const char *pattern = NULL;

    (void)ctx;
    if (nargs >= 1 && args[0].type == VAL_CHAR) pattern = args[0].str;

    dir = opendir(path);
    if (!dir) {
        *result = val_num(0);
        return 0;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' ||
            (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;  /* skip . and .. */
        if (pattern) {
            if (str_like(ent->d_name, pattern)) count++;
        } else {
            count++;
        }
    }
    closedir(dir);

    *result = val_num((double)count);
    return 0;
}

static int fn_version(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_str("dBASE III Clone");
    return 0;
}

static int fn_os(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_str("SLOW-32");
    return 0;
}

/* ---- INKEY / LASTKEY / READKEY ---- */
static int fn_inkey(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    double timeout;
    (void)ctx;
    /* INKEY() = poll, INKEY(0) = wait forever, INKEY(n) = wait n seconds */
    if (nargs >= 1 && args[0].type == VAL_NUM) {
        timeout = args[0].num;
        if (timeout == 0.0) timeout = -1.0;  /* dBase: INKEY(0) = wait forever */
    } else {
        timeout = 0.0;  /* INKEY() with no args = poll */
    }
    *result = val_num((double)screen_inkey(timeout));
    return 0;
}

static int fn_lastkey(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num((double)screen_lastkey());
    return 0;
}

static int fn_readkey(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num((double)screen_readkey());
    return 0;
}

/* ---- ERROR / MESSAGE / LINENO / PROGRAM ---- */
static int fn_error(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num((double)prog_get_error_code());
    return 0;
}

static int fn_message(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_str(prog_get_error_message());
    return 0;
}

static int fn_lineno(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num((double)prog_get_lineno());
    return 0;
}

static int fn_program(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_str(prog_get_program_name());
    return 0;
}

/* ---- Database info functions ---- */

static int fn_select(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num((double)(area_get_current_idx() + 1));
    return 0;
}

static int fn_alias(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    work_area_t *wa;
    (void)ctx;
    if (nargs >= 1 && args[0].type == VAL_NUM) {
        int idx = (int)args[0].num - 1;
        wa = area_get(idx);
    } else {
        wa = area_get_current();
    }
    if (wa && wa->alias[0])
        *result = val_str(wa->alias);
    else
        *result = val_str("");
    return 0;
}

static int fn_dbf(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    dbf_t *db;
    (void)ctx;
    if (nargs >= 1 && args[0].type == VAL_CHAR) {
        int idx = area_resolve_alias(args[0].str);
        if (idx >= 0) db = &area_get(idx)->db;
        else { *result = val_str(""); return 0; }
    } else {
        db = &area_get_current()->db;
    }
    if (dbf_is_open(db))
        *result = val_str(db->filename);
    else
        *result = val_str("");
    return 0;
}

static int fn_used(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    dbf_t *db;
    (void)ctx;
    if (nargs >= 1 && args[0].type == VAL_CHAR) {
        int idx = area_resolve_alias(args[0].str);
        if (idx >= 0) db = &area_get(idx)->db;
        else { *result = val_logic(0); return 0; }
    } else {
        db = &area_get_current()->db;
    }
    *result = val_logic(dbf_is_open(db));
    return 0;
}

static int fn_field(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    dbf_t *db;
    int n;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_NUM) {
        ctx->error = "FIELD requires (number [, alias])";
        return -1;
    }
    n = (int)args[0].num;
    if (nargs >= 2 && args[1].type == VAL_CHAR) {
        int idx = area_resolve_alias(args[1].str);
        if (idx >= 0) db = &area_get(idx)->db;
        else { *result = val_str(""); return 0; }
    } else {
        db = &area_get_current()->db;
    }
    if (!dbf_is_open(db) || n < 1 || n > db->field_count) {
        *result = val_str("");
        return 0;
    }
    *result = val_str(db->fields[n - 1].name);
    return 0;
}

static int fn_fcount(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    dbf_t *db;
    (void)ctx;
    if (nargs >= 1 && args[0].type == VAL_CHAR) {
        int idx = area_resolve_alias(args[0].str);
        if (idx >= 0) db = &area_get(idx)->db;
        else { *result = val_num(0); return 0; }
    } else {
        db = &area_get_current()->db;
    }
    if (!dbf_is_open(db))
        *result = val_num(0);
    else
        *result = val_num((double)db->field_count);
    return 0;
}

static int fn_fsize(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    dbf_t *db;
    int idx;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "FSIZE requires (field_name)";
        return -1;
    }
    db = &area_get_current()->db;
    if (!dbf_is_open(db)) { *result = val_num(0); return 0; }
    idx = dbf_find_field(db, args[0].str);
    if (idx < 0) { *result = val_num(0); return 0; }
    *result = val_num((double)db->fields[idx].length);
    return 0;
}

static int fn_fieldnum(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    dbf_t *db;
    int idx;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "FIELDNUM requires (field_name)";
        return -1;
    }
    db = &area_get_current()->db;
    if (!dbf_is_open(db)) { *result = val_num(0); return 0; }
    idx = dbf_find_field(db, args[0].str);
    *result = val_num((double)(idx >= 0 ? idx + 1 : 0));
    return 0;
}

static int fn_ndx(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    work_area_t *wa;
    int n;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_NUM) {
        ctx->error = "NDX requires (number [, alias])";
        return -1;
    }
    n = (int)args[0].num;
    if (nargs >= 2 && args[1].type == VAL_CHAR) {
        int idx = area_resolve_alias(args[1].str);
        if (idx >= 0) wa = area_get(idx);
        else { *result = val_str(""); return 0; }
    } else {
        wa = area_get_current();
    }
    if (n >= 1 && n <= wa->num_indexes && wa->indexes[n - 1].active)
        *result = val_str(wa->indexes[n - 1].filename);
    else
        *result = val_str("");
    return 0;
}

static int fn_key(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    work_area_t *wa;
    index_t *idx;
    (void)ctx;
    if (nargs >= 1 && args[0].type == VAL_NUM) {
        int n = (int)args[0].num - 1;
        wa = area_get(n >= 0 && n < 10 ? n : area_get_current_idx());
    } else {
        wa = area_get_current();
    }
    if (wa->order > 0 && wa->order <= wa->num_indexes) {
        idx = &wa->indexes[wa->order - 1];
        *result = val_str(idx->key_expr);
    } else {
        *result = val_str("");
    }
    return 0;
}

static int fn_order(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    work_area_t *wa;
    (void)ctx;
    if (nargs >= 1 && args[0].type == VAL_CHAR) {
        int idx = area_resolve_alias(args[0].str);
        if (idx >= 0) wa = area_get(idx);
        else { *result = val_num(0); return 0; }
    } else {
        wa = area_get_current();
    }
    *result = val_num((double)wa->order);
    return 0;
}

static int fn_lupdate(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    dbf_t *db;
    (void)args; (void)nargs;
    db = ctx->db;
    if (!db || !dbf_is_open(db)) {
        *result = val_date(0);
        return 0;
    }
    /* Read bytes 1-3 from DBF header: YY, MM, DD */
    {
        long pos = ftell(db->fp);
        unsigned char hdr[4];
        fseek(db->fp, 1, SEEK_SET);
        if (fread(hdr, 1, 3, db->fp) == 3) {
            int y = hdr[0] + 1900;
            if (y < 1980) y += 100; /* Y2K: years < 80 are 2000s */
            int m = hdr[1];
            int d = hdr[2];
            *result = val_date(date_to_jdn(y, m, d));
        } else {
            *result = val_date(0);
        }
        fseek(db->fp, pos, SEEK_SET);
    }
    return 0;
}

static int fn_recsize(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)args; (void)nargs;
    if (!ctx->db || !dbf_is_open(ctx->db))
        *result = val_num(0);
    else
        *result = val_num((double)ctx->db->record_size);
    return 0;
}

/* ---- Additional string functions ---- */

static int fn_occurs(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_CHAR || args[1].type != VAL_CHAR) {
        ctx->error = "OCCURS requires (substring, string)";
        return -1;
    }
    {
        const char *sub = args[0].str;
        const char *s = args[1].str;
        int sublen = strlen(sub);
        int count = 0;
        const char *p;
        if (sublen == 0) { *result = val_num(0); return 0; }
        p = s;
        while ((p = strstr(p, sub)) != NULL) {
            count++;
            p += sublen;
        }
        *result = val_num((double)count);
    }
    return 0;
}

static int fn_rat(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_CHAR || args[1].type != VAL_CHAR) {
        ctx->error = "RAT requires (substring, string)";
        return -1;
    }
    {
        const char *sub = args[0].str;
        const char *s = args[1].str;
        int sublen = strlen(sub);
        int slen = strlen(s);
        int i;
        if (sublen == 0 || sublen > slen) { *result = val_num(0); return 0; }
        for (i = slen - sublen; i >= 0; i--) {
            if (memcmp(s + i, sub, sublen) == 0) {
                *result = val_num((double)(i + 1));
                return 0;
            }
        }
        *result = val_num(0);
    }
    return 0;
}

static int fn_proper(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    int i, cap_next;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "PROPER requires (string)";
        return -1;
    }
    str_copy(buf, args[0].str, sizeof(buf));
    cap_next = 1;
    for (i = 0; buf[i]; i++) {
        if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '-' || buf[i] == '\'') {
            cap_next = 1;
        } else {
            if (cap_next) {
                if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32;
                cap_next = 0;
            } else {
                if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] += 32;
            }
        }
    }
    *result = val_str(buf);
    return 0;
}

static int fn_strtran(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[512];
    int pos = 0;
    (void)ctx;
    if (nargs < 3 || args[0].type != VAL_CHAR || args[1].type != VAL_CHAR || args[2].type != VAL_CHAR) {
        ctx->error = "STRTRAN requires (string, from, to)";
        return -1;
    }
    {
        const char *s = args[0].str;
        const char *from = args[1].str;
        const char *to = args[2].str;
        int fromlen = strlen(from);
        int tolen = strlen(to);
        const char *p;
        if (fromlen == 0) { *result = val_str(s); return 0; }
        while (*s && pos < (int)sizeof(buf) - 1) {
            p = strstr(s, from);
            if (p) {
                int chunk = (int)(p - s);
                if (pos + chunk > (int)sizeof(buf) - 1) chunk = (int)sizeof(buf) - 1 - pos;
                memcpy(buf + pos, s, chunk);
                pos += chunk;
                if (pos + tolen <= (int)sizeof(buf) - 1) {
                    memcpy(buf + pos, to, tolen);
                    pos += tolen;
                }
                s = p + fromlen;
            } else {
                int rest = strlen(s);
                if (pos + rest > (int)sizeof(buf) - 1) rest = (int)sizeof(buf) - 1 - pos;
                memcpy(buf + pos, s, rest);
                pos += rest;
                break;
            }
        }
        buf[pos] = '\0';
    }
    *result = val_str(buf);
    return 0;
}

static int fn_between(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 3 || args[0].type != VAL_NUM || args[1].type != VAL_NUM || args[2].type != VAL_NUM) {
        ctx->error = "BETWEEN requires (value, low, high)";
        return -1;
    }
    *result = val_logic(args[0].num >= args[1].num && args[0].num <= args[2].num);
    return 0;
}

static int fn_soundex(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[5];
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "SOUNDEX requires (string)";
        return -1;
    }
    {
        /* Standard Soundex: letter -> digit mapping */
        static const char map[] = "01230120022455012623010202";
        const char *s = args[0].str;
        int i, bi = 1;
        char last;
        /* Skip to first letter */
        while (*s && !((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'))) s++;
        if (!*s) { *result = val_str("0000"); return 0; }
        buf[0] = (*s >= 'a' && *s <= 'z') ? *s - 32 : *s;
        last = map[buf[0] - 'A'];
        s++;
        while (*s && bi < 4) {
            char c = *s++;
            if (c >= 'a' && c <= 'z') c -= 32;
            if (c >= 'A' && c <= 'Z') {
                char code = map[c - 'A'];
                if (code != '0' && code != last) {
                    buf[bi++] = code;
                    last = code;
                } else if (code == '0') {
                    last = '0';
                }
            }
        }
        while (bi < 4) buf[bi++] = '0';
        buf[4] = '\0';
    }
    *result = val_str(buf);
    return 0;
}

/* ---- Date/Time functions ---- */

static int fn_time(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[12];
    time_t t;
    struct tm *tm;
    (void)ctx; (void)args; (void)nargs;
    t = time(NULL);
    tm = localtime(&t);
    if (tm) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        str_copy(buf, "00:00:00", sizeof(buf));
    }
    *result = val_str(buf);
    return 0;
}

static int fn_seconds(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    time_t t;
    struct tm *tm;
    (void)ctx; (void)args; (void)nargs;
    t = time(NULL);
    tm = localtime(&t);
    if (tm)
        *result = val_num((double)(tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec));
    else
        *result = val_num(0);
    return 0;
}

/* ---- System stubs ---- */

static int fn_rlock(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_logic(1);  /* single-user: always succeeds */
    return 0;
}

static int fn_getenv(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_CHAR) {
        ctx->error = "GETENV requires (name)";
        return -1;
    }
    {
        const char *val = getenv(args[0].str);
        *result = val_str(val ? val : "");
    }
    return 0;
}

/* ---- SEEK() function form ---- */

static int fn_seek_func(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    index_t *idx;
    char key[MAX_INDEX_KEY + 1];
    (void)ctx;
    if (nargs < 1) {
        ctx->error = "SEEK requires (expr)";
        return -1;
    }
    idx = cmd_controlling_index();
    if (!idx) {
        *result = val_logic(0);
        return 0;
    }
    index_format_key_value(idx->key_type, &args[0], key, sizeof(key));
    if (idx->key_type == 0)
        trim_right(key);
    if (index_seek(idx, key, 0)) {
        uint32_t rec = index_current_recno(idx);
        ctx->found = 1;
        if (rec > 0 && ctx->db) {
            dbf_read_record(ctx->db, rec);
            ctx->eof_flag = 0;
            ctx->bof_flag = 0;
        }
        *result = val_logic(1);
    } else {
        ctx->found = 0;
        {
            uint32_t rec = index_current_recno(idx);
            if (rec > 0 && ctx->db) {
                dbf_read_record(ctx->db, rec);
                ctx->eof_flag = 0;
            } else {
                ctx->eof_flag = 1;
            }
        }
        *result = val_logic(0);
    }
    return 0;
}

/* ---- Tier 2 compatibility functions ---- */

/* val_compare helper: compare two values, returns -1/0/1 */
static int val_compare(const value_t *a, const value_t *b) {
    if (a->type == VAL_NUM && b->type == VAL_NUM) {
        if (a->num < b->num) return -1;
        if (a->num > b->num) return 1;
        return 0;
    }
    if (a->type == VAL_CHAR && b->type == VAL_CHAR) {
        return str_icmp(a->str, b->str);
    }
    if (a->type == VAL_DATE && b->type == VAL_DATE) {
        if (a->date < b->date) return -1;
        if (a->date > b->date) return 1;
        return 0;
    }
    if (a->type == VAL_LOGIC && b->type == VAL_LOGIC) {
        return a->logic - b->logic;
    }
    /* Different types: compare type enum values */
    return (int)a->type - (int)b->type;
}

/* ---- Array functions ---- */

static int fn_alen(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    if (nargs < 1 || args[0].type != VAL_ARRAY || !args[0].array) {
        ctx->error = "ALEN requires (array[,dim])";
        return -1;
    }
    {
        array_t *arr = args[0].array;
        int dim = (nargs >= 2 && args[1].type == VAL_NUM) ? (int)args[1].num : 0;
        if (dim == 1)
            *result = val_num((double)arr->rows);
        else if (dim == 2)
            *result = val_num((double)arr->cols);
        else
            *result = val_num((double)(arr->rows * (arr->cols > 0 ? arr->cols : 1)));
    }
    return 0;
}

static int fn_ascan(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    if (nargs < 2 || args[0].type != VAL_ARRAY || !args[0].array) {
        ctx->error = "ASCAN requires (array, value)";
        return -1;
    }
    {
        array_t *arr = args[0].array;
        int total = arr->rows * (arr->cols > 0 ? arr->cols : 1);
        int i;
        for (i = 0; i < total; i++) {
            if (val_compare(&arr->elements[i], &args[1]) == 0) {
                *result = val_num((double)(i + 1));
                return 0;
            }
        }
        *result = val_num(0);
    }
    return 0;
}

static int fn_asort(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    if (nargs < 1 || args[0].type != VAL_ARRAY || !args[0].array) {
        ctx->error = "ASORT requires (array)";
        return -1;
    }
    {
        array_t *arr = args[0].array;
        int total = arr->rows * (arr->cols > 0 ? arr->cols : 1);
        int i, j;
        /* Insertion sort — stable, in-place */
        for (i = 1; i < total; i++) {
            value_t tmp = arr->elements[i];
            j = i - 1;
            while (j >= 0 && val_compare(&arr->elements[j], &tmp) > 0) {
                arr->elements[j + 1] = arr->elements[j];
                j--;
            }
            arr->elements[j + 1] = tmp;
        }
        *result = val_num((double)total);
    }
    return 0;
}

static int fn_acopy(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    if (nargs < 2 || args[0].type != VAL_ARRAY || !args[0].array ||
        args[1].type != VAL_ARRAY || !args[1].array) {
        ctx->error = "ACOPY requires (source, dest[, start, count, dstart])";
        return -1;
    }
    {
        array_t *src = args[0].array;
        array_t *dst = args[1].array;
        int src_total = src->rows * (src->cols > 0 ? src->cols : 1);
        int dst_total = dst->rows * (dst->cols > 0 ? dst->cols : 1);
        int start  = (nargs >= 3 && args[2].type == VAL_NUM) ? (int)args[2].num : 1;
        int count  = (nargs >= 4 && args[3].type == VAL_NUM) ? (int)args[3].num : (src_total - start + 1);
        int dstart = (nargs >= 5 && args[4].type == VAL_NUM) ? (int)args[4].num : 1;
        int copied = 0, i;

        for (i = 0; i < count; i++) {
            int si = start - 1 + i;
            int di = dstart - 1 + i;
            if (si < 0 || si >= src_total) break;
            if (di < 0 || di >= dst_total) break;
            dst->elements[di] = src->elements[si];
            copied++;
        }
        *result = val_num((double)copied);
    }
    return 0;
}

static int fn_ains(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    if (nargs < 2 || args[0].type != VAL_ARRAY || !args[0].array ||
        args[1].type != VAL_NUM) {
        ctx->error = "AINS requires (array, element)";
        return -1;
    }
    {
        array_t *arr = args[0].array;
        int total = arr->rows * (arr->cols > 0 ? arr->cols : 1);
        int n = (int)args[1].num - 1;  /* 0-based */
        int i;
        if (n < 0 || n >= total) {
            ctx->error = "AINS element out of range";
            return -1;
        }
        for (i = total - 1; i > n; i--)
            arr->elements[i] = arr->elements[i - 1];
        arr->elements[n] = val_str("");
        *result = val_logic(1);
    }
    return 0;
}

static int fn_adel(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    if (nargs < 2 || args[0].type != VAL_ARRAY || !args[0].array ||
        args[1].type != VAL_NUM) {
        ctx->error = "ADEL requires (array, element)";
        return -1;
    }
    {
        array_t *arr = args[0].array;
        int total = arr->rows * (arr->cols > 0 ? arr->cols : 1);
        int n = (int)args[1].num - 1;  /* 0-based */
        int i;
        if (n < 0 || n >= total) {
            ctx->error = "ADEL element out of range";
            return -1;
        }
        for (i = n; i < total - 1; i++)
            arr->elements[i] = arr->elements[i + 1];
        arr->elements[total - 1] = val_str("");
        *result = val_logic(1);
    }
    return 0;
}

static int fn_afields(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    if (nargs < 1 || args[0].type != VAL_ARRAY || !args[0].array) {
        ctx->error = "AFIELDS requires (array)";
        return -1;
    }
    if (!ctx->db || !dbf_is_open(ctx->db)) {
        *result = val_num(0);
        return 0;
    }
    {
        array_t *arr = args[0].array;
        int fc = ctx->db->field_count;
        int total = arr->rows * (arr->cols > 0 ? arr->cols : 1);
        int is_2d = (arr->cols >= 4);
        int i;
        for (i = 0; i < fc && i < (is_2d ? arr->rows : total); i++) {
            if (is_2d) {
                arr->elements[i * arr->cols + 0] = val_str(ctx->db->fields[i].name);
                {
                    char t[2]; t[0] = ctx->db->fields[i].type; t[1] = '\0';
                    arr->elements[i * arr->cols + 1] = val_str(t);
                }
                arr->elements[i * arr->cols + 2] = val_num((double)ctx->db->fields[i].length);
                arr->elements[i * arr->cols + 3] = val_num((double)ctx->db->fields[i].decimals);
            } else {
                arr->elements[i] = val_str(ctx->db->fields[i].name);
            }
        }
        *result = val_num((double)fc);
    }
    return 0;
}

/* ---- String functions ---- */

static int fn_like(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_CHAR || args[1].type != VAL_CHAR) {
        ctx->error = "LIKE requires (pattern, string)";
        return -1;
    }
    /* str_like(text, pattern): note LIKE(pattern, string) reverses args */
    *result = val_logic(str_like(args[1].str, args[0].str));
    return 0;
}

static int fn_chrtran(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[256];
    int bi = 0;
    (void)ctx;
    if (nargs < 3 || args[0].type != VAL_CHAR || args[1].type != VAL_CHAR || args[2].type != VAL_CHAR) {
        ctx->error = "CHRTRAN requires (string, from, to)";
        return -1;
    }
    {
        const char *s = args[0].str;
        const char *from = args[1].str;
        const char *to = args[2].str;
        int tolen = strlen(to);
        while (*s && bi < (int)sizeof(buf) - 1) {
            const char *f = strchr(from, *s);
            if (f) {
                int pos = (int)(f - from);
                if (pos < tolen)
                    buf[bi++] = to[pos];
                /* else: delete character (no replacement) */
            } else {
                buf[bi++] = *s;
            }
            s++;
        }
        buf[bi] = '\0';
    }
    *result = val_str(buf);
    return 0;
}

static void compute_soundex(const char *input, char *out) {
    static const char map[] = "01230120022455012623010202";
    const char *s = input;
    int bi = 1;
    char last;
    while (*s && !((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'))) s++;
    if (!*s) { out[0] = '0'; out[1] = '0'; out[2] = '0'; out[3] = '0'; out[4] = '\0'; return; }
    out[0] = (*s >= 'a' && *s <= 'z') ? *s - 32 : *s;
    last = map[out[0] - 'A'];
    s++;
    while (*s && bi < 4) {
        char c = *s++;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c >= 'A' && c <= 'Z') {
            char code = map[c - 'A'];
            if (code != '0' && code != last) {
                out[bi++] = code;
                last = code;
            } else if (code == '0') {
                last = '0';
            }
        }
    }
    while (bi < 4) out[bi++] = '0';
    out[4] = '\0';
}

static int fn_difference(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char s1[5], s2[5];
    int count = 0, i;
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_CHAR || args[1].type != VAL_CHAR) {
        ctx->error = "DIFFERENCE requires (string1, string2)";
        return -1;
    }
    compute_soundex(args[0].str, s1);
    compute_soundex(args[1].str, s2);
    for (i = 0; i < 4; i++) {
        if (s1[i] == s2[i]) count++;
    }
    *result = val_num((double)count);
    return 0;
}

/* ---- Date functions ---- */

static int fn_gomonth(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx;
    if (nargs < 2 || args[0].type != VAL_DATE || args[1].type != VAL_NUM) {
        ctx->error = "GOMONTH requires (date, months)";
        return -1;
    }
    if (args[0].date == 0) {
        *result = val_date(0);
        return 0;
    }
    {
        int y, m, d, n, dim;
        date_from_jdn(args[0].date, &y, &m, &d);
        n = (int)args[1].num;
        m += n;
        while (m > 12) { m -= 12; y++; }
        while (m < 1) { m += 12; y--; }
        dim = date_days_in_month(y, m);
        if (d > dim) d = dim;
        *result = val_date(date_to_jdn(y, m, d));
    }
    return 0;
}

static int fn_dmy(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[32];
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_DATE) {
        ctx->error = "DMY requires (date)";
        return -1;
    }
    if (args[0].date == 0) {
        *result = val_str("");
        return 0;
    }
    {
        int y, m, d;
        date_from_jdn(args[0].date, &y, &m, &d);
        snprintf(buf, sizeof(buf), "%02d %s %04d", d, date_month_name(m), y);
    }
    *result = val_str(buf);
    return 0;
}

static int fn_mdy(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    char buf[32];
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_DATE) {
        ctx->error = "MDY requires (date)";
        return -1;
    }
    if (args[0].date == 0) {
        *result = val_str("");
        return 0;
    }
    {
        int y, m, d;
        date_from_jdn(args[0].date, &y, &m, &d);
        snprintf(buf, sizeof(buf), "%s %02d, %04d", date_month_name(m), d, y);
    }
    *result = val_str(buf);
    return 0;
}

/* ---- System stubs ---- */

static int fn_header(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)args; (void)nargs;
    if (ctx->db && dbf_is_open(ctx->db))
        *result = val_num((double)ctx->db->header_size);
    else
        *result = val_num(0);
    return 0;
}

static int fn_memory(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num(640);
    return 0;
}

static int fn_diskspace(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)ctx; (void)args; (void)nargs;
    *result = val_num(100000000);
    return 0;
}

/* ---- Tier 3: LOOKUP ---- */
static int fn_lookup(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    if (nargs < 3 || args[0].type != VAL_CHAR ||
        args[1].type != VAL_CHAR || args[2].type != VAL_CHAR) {
        ctx->error = "LOOKUP requires (ret_field, search_value, search_field)";
        return -1;
    }
    if (!ctx->db || !dbf_is_open(ctx->db)) {
        *result = val_str("");
        return 0;
    }
    {
        const char *ret_name = args[0].str;
        const char *search_val = args[1].str;
        const char *search_name = args[2].str;
        uint32_t saved_rec = ctx->db->current_record;
        uint32_t i;
        int found = 0;

        /* Find field indices */
        int ret_idx = -1, search_idx = -1;
        for (i = 0; i < (uint32_t)ctx->db->field_count; i++) {
            if (str_icmp(ctx->db->fields[i].name, ret_name) == 0) ret_idx = (int)i;
            if (str_icmp(ctx->db->fields[i].name, search_name) == 0) search_idx = (int)i;
        }
        if (ret_idx < 0 || search_idx < 0) {
            *result = val_str("");
            return 0;
        }

        for (i = 1; i <= ctx->db->record_count; i++) {
            char fval[256];
            dbf_read_record(ctx->db, i);
            if (ctx->db->record_buf[0] == '*') continue;
            dbf_get_field_raw(ctx->db, search_idx, fval, sizeof(fval));
            trim_right(fval);
            if (str_icmp(fval, search_val) == 0) {
                char rval[256];
                dbf_get_field_raw(ctx->db, ret_idx, rval, sizeof(rval));
                trim_right(rval);
                *result = val_str(rval);
                found = 1;
                break;
            }
        }
        /* Restore original position */
        if (saved_rec > 0 && saved_rec <= ctx->db->record_count)
            dbf_read_record(ctx->db, saved_rec);
        if (!found) *result = val_str("");
    }
    return 0;
}

/* ---- Tier 3: SYS() ---- */
static int fn_sys(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    int num;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_NUM) {
        ctx->error = "SYS requires (number)";
        return -1;
    }
    num = (int)args[0].num;
    switch (num) {
    case 0:    *result = val_str("0"); break;
    case 1: {
        /* Julian day */
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        int y = tm->tm_year + 1900, m = tm->tm_mon + 1, d = tm->tm_mday;
        int jd = (1461 * (y + 4800 + (m - 14)/12))/4 +
                 (367 * (m - 2 - 12 * ((m - 14)/12)))/12 -
                 (3 * ((y + 4900 + (m - 14)/12)/100))/4 + d - 32075;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", jd);
        *result = val_str(buf);
        break;
    }
    case 2: {
        /* Seconds since midnight */
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        int secs = tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", secs);
        *result = val_str(buf);
        break;
    }
    case 3:    *result = val_str("0"); break;
    case 5:    *result = val_str("C:"); break;
    case 6:    *result = val_str("PRN"); break;
    case 10:   *result = val_str("640"); break;
    case 16: {
        const char *name = prog_get_program_name();
        *result = val_str(name ? name : "");
        break;
    }
    case 17:   *result = val_str("SLOW-32"); break;
    case 2001: *result = val_str("dBASE III Clone 1.0"); break;
    default:   *result = val_str(""); break;
    }
    return 0;
}

/* ---- Tier 3: SET() ---- */
static int fn_set(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    int num;
    const set_options_t *opts;
    (void)ctx;
    if (nargs < 1 || args[0].type != VAL_NUM) {
        ctx->error = "SET requires (number)";
        return -1;
    }
    num = (int)args[0].num;
    opts = cmd_get_set_opts();
    switch (num) {
    case 1:  *result = val_str(opts->talk ? "ON" : "OFF"); break;
    case 2:  *result = val_str(opts->heading ? "ON" : "OFF"); break;
    case 3:  *result = val_str(opts->exact ? "ON" : "OFF"); break;
    case 4:  *result = val_str(opts->deleted ? "ON" : "OFF"); break;
    case 5:  *result = val_str(opts->confirm ? "ON" : "OFF"); break;
    case 6:  *result = val_str(opts->bell ? "ON" : "OFF"); break;
    case 7:  *result = val_str(opts->safety ? "ON" : "OFF"); break;
    case 8:  *result = val_str(opts->console ? "ON" : "OFF"); break;
    case 9:  *result = val_str(opts->escape ? "ON" : "OFF"); break;
    case 10: {
        const char *names[] = {"AMERICAN","ANSI","BRITISH","FRENCH","GERMAN","ITALIAN","JAPAN"};
        int idx = (int)opts->date_format;
        if (idx >= 0 && idx < 7) *result = val_str(names[idx]);
        else *result = val_str("AMERICAN");
        break;
    }
    case 11: { char b[8]; snprintf(b,sizeof(b),"%d",opts->decimals); *result = val_str(b); break; }
    case 12: *result = val_str(opts->century ? "ON" : "OFF"); break;
    case 13: *result = val_str(opts->unique ? "ON" : "OFF"); break;
    case 14: *result = val_str(opts->softseek ? "ON" : "OFF"); break;
    case 15: *result = val_str(opts->path); break;
    case 16: *result = val_str(opts->alternate_on ? "ON" : "OFF"); break;
    case 17: *result = val_str(opts->device ? "PRINT" : "SCREEN"); break;
    case 18: { char b[8]; snprintf(b,sizeof(b),"%d",opts->margin); *result = val_str(b); break; }
    case 19: { char b[8]; snprintf(b,sizeof(b),"%d",opts->memowidth); *result = val_str(b); break; }
    case 20: { char b[8]; snprintf(b,sizeof(b),"%d",opts->epoch); *result = val_str(b); break; }
    case 21: {
        if (opts->mark) { char b[2]; b[0] = opts->mark; b[1] = '\0'; *result = val_str(b); }
        else *result = val_str("");
        break;
    }
    case 22: *result = val_str(opts->wrap ? "ON" : "OFF"); break;
    default: *result = val_str(""); break;
    }
    return 0;
}

/* ---- Tier 3: DBSTRUCT() ---- */
static int fn_dbstruct(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result) {
    (void)args; (void)nargs;
    if (!ctx->db || !dbf_is_open(ctx->db)) {
        *result = val_num(0);
        return 0;
    }
    {
        int fc = ctx->db->field_count;
        int i;
        array_t *arr = (array_t *)calloc(1, sizeof(array_t));
        if (!arr) { *result = val_num(0); return 0; }
        arr->rows = fc;
        arr->cols = 4;
        arr->elements = (value_t *)calloc(fc * 4, sizeof(value_t));
        if (!arr->elements) { free(arr); *result = val_num(0); return 0; }
        for (i = 0; i < fc; i++) {
            char t[2];
            arr->elements[i * 4 + 0] = val_str(ctx->db->fields[i].name);
            t[0] = ctx->db->fields[i].type; t[1] = '\0';
            arr->elements[i * 4 + 1] = val_str(t);
            arr->elements[i * 4 + 2] = val_num((double)ctx->db->fields[i].length);
            arr->elements[i * 4 + 3] = val_num((double)ctx->db->fields[i].decimals);
        }
        result->type = VAL_ARRAY;
        result->array = arr;
    }
    return 0;
}

/* ---- Dispatch table ---- */

typedef int (*func_impl_t)(expr_ctx_t *ctx, value_t *args, int nargs, value_t *result);

typedef struct {
    const char *name;
    func_impl_t fn;
} func_entry_t;

static const func_entry_t func_table[] = {
    /* Status */
    { "EOF",       fn_eof },
    { "BOF",       fn_bof },
    { "RECNO",     fn_recno },
    { "RECCOUNT",  fn_reccount },
    { "DELETED",   fn_deleted },
    { "FOUND",     fn_found },
    /* String */
    { "SUBSTR",    fn_substr },
    { "TRIM",      fn_trim },
    { "RTRIM",     fn_trim },     /* alias */
    { "LTRIM",     fn_ltrim },
    { "ALLTRIM",   fn_alltrim },
    { "UPPER",     fn_upper },
    { "LOWER",     fn_lower },
    { "AT",        fn_at },
    { "LEN",       fn_len },
    { "SPACE",     fn_space },
    { "REPLICATE", fn_replicate },
    { "PADL",      fn_padl },
    { "PADR",      fn_padr },
    { "PADC",      fn_padc },
    { "LEFT",      fn_left },
    { "RIGHT",     fn_right },
    { "STR",       fn_str },
    { "VAL",       fn_val },
    { "CHR",       fn_chr },
    { "ASC",       fn_asc },
    { "TYPE",      fn_type },
    /* Numeric */
    { "INT",       fn_int },
    { "ROUND",     fn_round },
    { "ABS",       fn_abs },
    { "MOD",       fn_mod },
    { "SQRT",      fn_sqrt },
    { "MAX",       fn_max },
    { "MIN",       fn_min },
    /* Date */
    { "DATE",      fn_date },
    { "DTOC",      fn_dtoc },
    { "CTOD",      fn_ctod },
    { "DAY",       fn_day },
    { "MONTH",     fn_month },
    { "YEAR",      fn_year },
    { "DOW",       fn_dow },
    { "CDOW",      fn_cdow },
    { "CMONTH",    fn_cmonth },
    { "DTOS",      fn_dtos },
    /* Screen */
    { "ROW",       fn_row },
    { "COL",       fn_col },
    { "PROW",      fn_prow },
    { "PCOL",      fn_pcol },
    /* Popup menu */
    { "BAR",       fn_bar },
    { "PROMPT",    fn_prompt_func },
    /* Menu bar */
    { "PAD",       fn_pad },
    { "POPUP",     fn_popup_func },
    /* String manipulation */
    { "STUFF",     fn_stuff },
    { "TRANSFORM", fn_transform },
    { "ISALPHA",   fn_isalpha },
    { "ISUPPER",   fn_isupper },
    { "ISLOWER",   fn_islower },
    /* Directory and File Services */
    { "ADIR",      fn_adir },
    { "CURDIR",    fn_curdir },
    { "FILE",      fn_file },
    /* Low-level File I/O */
    { "FOPEN",     fn_fopen },
    { "FCREATE",   fn_fcreate },
    { "FCLOSE",    fn_fclose },
    { "FREAD",     fn_fread },
    { "FWRITE",    fn_fwrite },
    { "FSEEK",     fn_fseek },
    { "FERROR",    fn_ferror },
    /* Misc */
    { "EMPTY",     fn_empty },
    { "IIF",       fn_iif },
    /* System */
    { "VERSION",   fn_version },
    { "OS",        fn_os },
    /* Keyboard */
    { "INKEY",     fn_inkey },
    { "LASTKEY",   fn_lastkey },
    { "READKEY",   fn_readkey },
    /* Debug */
    { "ERROR",     fn_error },
    { "MESSAGE",   fn_message },
    { "LINENO",    fn_lineno },
    { "PROGRAM",   fn_program },
    /* Database info */
    { "SELECT",    fn_select },
    { "ALIAS",     fn_alias },
    { "DBF",       fn_dbf },
    { "USED",      fn_used },
    { "FIELD",     fn_field },
    { "FCOUNT",    fn_fcount },
    { "FSIZE",     fn_fsize },
    { "FIELDNUM",  fn_fieldnum },
    { "FIELDPOS",  fn_fieldnum },   /* alias */
    { "NDX",       fn_ndx },
    { "KEY",       fn_key },
    { "ORDER",     fn_order },
    { "LUPDATE",   fn_lupdate },
    { "RECSIZE",   fn_recsize },
    /* Additional string */
    { "OCCURS",    fn_occurs },
    { "RAT",       fn_rat },
    { "PROPER",    fn_proper },
    { "STRTRAN",   fn_strtran },
    { "BETWEEN",   fn_between },
    { "SOUNDEX",   fn_soundex },
    /* Date/Time */
    { "TIME",      fn_time },
    { "SECONDS",   fn_seconds },
    /* System stubs */
    { "RLOCK",     fn_rlock },
    { "FLOCK",     fn_rlock },      /* alias - same behavior */
    { "GETENV",    fn_getenv },
    /* SEEK() function form */
    { "SEEK",      fn_seek_func },
    /* Tier 2: Array functions */
    { "ALEN",      fn_alen },
    { "ASCAN",     fn_ascan },
    { "ASORT",     fn_asort },
    { "ACOPY",     fn_acopy },
    { "AINS",      fn_ains },
    { "ADEL",      fn_adel },
    { "AFIELDS",   fn_afields },
    /* Tier 2: String functions */
    { "LIKE",      fn_like },
    { "CHRTRAN",   fn_chrtran },
    { "DIFFERENCE", fn_difference },
    /* Tier 2: Date functions */
    { "GOMONTH",   fn_gomonth },
    { "DMY",       fn_dmy },
    { "MDY",       fn_mdy },
    /* Tier 2: System stubs */
    { "HEADER",    fn_header },
    { "MEMORY",    fn_memory },
    { "DISKSPACE", fn_diskspace },
    /* Tier 3: Compatibility */
    { "LOOKUP",    fn_lookup },
    { "SYS",       fn_sys },
    { "SET",       fn_set },
    { "DBSTRUCT",  fn_dbstruct },
    { NULL, NULL }
};

static udf_callback_t udf_callback;

void func_set_udf_callback(udf_callback_t cb) {
    udf_callback = cb;
}

int func_call(expr_ctx_t *ctx, const char *name, value_t *args, int nargs, value_t *result) {
    const func_entry_t *e;
    char upper[64];
    int i;

    /* Uppercase the name for comparison */
    for (i = 0; name[i] && i < (int)sizeof(upper) - 1; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        upper[i] = c;
    }
    upper[i] = '\0';

    for (e = func_table; e->name; e++) {
        if (strcmp(upper, e->name) == 0)
            return e->fn(ctx, args, nargs, result);
    }

    /* Try user-defined function.
       Callback returns: 0=handled, >0=not found, <0=execution error. */
    if (udf_callback) {
        int urc = udf_callback(upper, args, nargs, result);
        if (urc == 0)
            return 0;
        if (urc < 0) {
            const char *msg = prog_get_error_message();
            if (msg && msg[0]) ctx->error = msg;
            else ctx->error = "User-defined function failed";
            return -1;
        }
    }

    {
        snprintf(ctx->err_msg, sizeof(ctx->err_msg), "Unknown function: %s", name);
        ctx->error = ctx->err_msg;
    }
    return -1;
}
