#include "builtin.h"
#include "eval.h"
#include "fileio.h"
#include "terminal.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>

/* Simple LCG random number generator */
static unsigned int rng_state = 12345;
static double last_rnd_val = 0.0;

static double sb_rnd(void) {
    rng_state = rng_state * 1103515245 + 12345;
    last_rnd_val = (double)(rng_state & 0x7FFFFFFF) / 2147483648.0;
    return last_rnd_val;
}

/* ================================================================
 * Helper: extract numeric argument
 * ================================================================ */
static error_t get_num(value_t *v, double *out) {
    if (v->type == VAL_STRING) return ERR_TYPE_MISMATCH;
    *out = (v->type == VAL_INTEGER) ? (double)v->ival : v->dval;
    return ERR_NONE;
}

/* ================================================================
 * Math built-in functions
 * ================================================================ */

static error_t fn_abs(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type == VAL_STRING) return ERR_TYPE_MISMATCH;
    if (args[0].type == VAL_INTEGER) {
        int v = args[0].ival;
        *out = val_integer(v < 0 ? -v : v);
    } else {
        double v = args[0].dval;
        *out = val_double(v < 0 ? -v : v);
    }
    return ERR_NONE;
}

static error_t fn_int(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_integer((int)floor(v));
    return ERR_NONE;
}

static error_t fn_sgn(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    if (v > 0) *out = val_integer(1);
    else if (v < 0) *out = val_integer(-1);
    else *out = val_integer(0);
    return ERR_NONE;
}

static error_t fn_sqr(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    if (v < 0) return ERR_ILLEGAL_FUNCTION_CALL;
    *out = val_double(sqrt(v));
    return ERR_NONE;
}

static error_t fn_fix(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_integer((int)v); /* truncate toward zero */
    return ERR_NONE;
}

static error_t fn_cint(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    /* Round to nearest integer (banker's rounding not needed for BASIC) */
    double rounded = v + (v >= 0.0 ? 0.5 : -0.5);
    if (rounded > 2147483647.0 || rounded < -2147483648.0)
        return ERR_OVERFLOW;
    *out = val_integer((int)rounded);
    return ERR_NONE;
}

static error_t fn_cdbl(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(v);
    return ERR_NONE;
}

/* Trig functions */
static error_t fn_sin(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(sin(v));
    return ERR_NONE;
}

static error_t fn_cos(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(cos(v));
    return ERR_NONE;
}

static error_t fn_tan(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(tan(v));
    return ERR_NONE;
}

static error_t fn_atn(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(atan(v));
    return ERR_NONE;
}

static error_t fn_atn2(value_t *args, int nargs, value_t *out) {
    if (nargs != 2) return ERR_ILLEGAL_FUNCTION_CALL;
    double y, x;
    EVAL_CHECK(get_num(&args[0], &y));
    EVAL_CHECK(get_num(&args[1], &x));
    *out = val_double(atan2(y, x));
    return ERR_NONE;
}

static error_t fn_asin(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(asin(v));
    return ERR_NONE;
}

static error_t fn_acos(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(acos(v));
    return ERR_NONE;
}

/* Hyperbolic functions */
static error_t fn_sinh(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(sinh(v));
    return ERR_NONE;
}

static error_t fn_cosh(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(cosh(v));
    return ERR_NONE;
}

static error_t fn_tanh(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(tanh(v));
    return ERR_NONE;
}

/* Inverse hyperbolic functions */
static error_t fn_asinh(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(log(v + sqrt(v * v + 1.0)));
    return ERR_NONE;
}

static error_t fn_acosh(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    if (v < 1.0) return ERR_ILLEGAL_FUNCTION_CALL;
    *out = val_double(log(v + sqrt(v * v - 1.0)));
    return ERR_NONE;
}

static error_t fn_atanh(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    if (v <= -1.0 || v >= 1.0) return ERR_ILLEGAL_FUNCTION_CALL;
    *out = val_double(0.5 * log((1.0 + v) / (1.0 - v)));
    return ERR_NONE;
}

static error_t fn_exp(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(exp(v));
    return ERR_NONE;
}

static error_t fn_log(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    if (v <= 0.0) return ERR_ILLEGAL_FUNCTION_CALL;
    *out = val_double(log(v));
    return ERR_NONE;
}

static error_t fn_log10(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    if (v <= 0.0) return ERR_ILLEGAL_FUNCTION_CALL;
    *out = val_double(log10(v));
    return ERR_NONE;
}

static error_t fn_log2(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    if (v <= 0.0) return ERR_ILLEGAL_FUNCTION_CALL;
    *out = val_double(log2(v));
    return ERR_NONE;
}

static error_t fn_round(value_t *args, int nargs, value_t *out) {
    if (nargs < 1 || nargs > 2) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    int decimals = 0;
    if (nargs == 2) {
        EVAL_CHECK(val_to_integer(&args[1], &decimals));
    }
    if (decimals == 0) {
        *out = val_double(floor(v + 0.5));
    } else {
        double mul = 1.0;
        int d = decimals < 0 ? -decimals : decimals;
        for (int i = 0; i < d; i++) mul *= 10.0;
        if (decimals > 0)
            *out = val_double(floor(v * mul + 0.5) / mul);
        else
            *out = val_double(floor(v / mul + 0.5) * mul);
    }
    return ERR_NONE;
}

static error_t fn_rnd(value_t *args, int nargs, value_t *out) {
    if (nargs > 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (nargs == 1) {
        int seed;
        EVAL_CHECK(val_to_integer(&args[0], &seed));
        if (seed < 0) {
            rng_state = (unsigned int)(-seed);
            *out = val_double(sb_rnd());
            return ERR_NONE;
        } else if (seed == 0) {
            *out = val_double(last_rnd_val);
            return ERR_NONE;
        }
    }
    *out = val_double(sb_rnd());
    return ERR_NONE;
}

/* ================================================================
 * String built-in functions
 * ================================================================ */

static error_t fn_len(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    *out = val_integer(args[0].sval->len);
    return ERR_NONE;
}

static error_t fn_chr(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int code;
    EVAL_CHECK(val_to_integer(&args[0], &code));
    if (code < 0 || code > 255) return ERR_ILLEGAL_FUNCTION_CALL;
    char buf[2] = { (char)code, '\0' };
    *out = val_string(buf, 1);
    return ERR_NONE;
}

static error_t fn_asc(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    if (args[0].sval->len == 0) return ERR_ILLEGAL_FUNCTION_CALL;
    *out = val_integer((unsigned char)args[0].sval->data[0]);
    return ERR_NONE;
}

static error_t fn_str(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    char buf[64];
    EVAL_CHECK(val_to_string(&args[0], buf, sizeof(buf)));
    *out = val_string_cstr(buf);
    return ERR_NONE;
}

static error_t fn_val(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    const char *s = args[0].sval->data;
    /* Skip leading whitespace */
    while (*s == ' ' || *s == '\t') s++;
    /* Check for &H (hex), &O (octal), &B (binary) prefixes */
    if (s[0] == '&' && s[1]) {
        char prefix = toupper((unsigned char)s[1]);
        if (prefix == 'H') {
            *out = val_integer((int)strtol(s + 2, NULL, 16));
            return ERR_NONE;
        } else if (prefix == 'O') {
            *out = val_integer((int)strtol(s + 2, NULL, 8));
            return ERR_NONE;
        } else if (prefix == 'B') {
            *out = val_integer((int)strtol(s + 2, NULL, 2));
            return ERR_NONE;
        }
    }
    char *endp;
    long lv = strtol(s, &endp, 10);
    if (*endp == '\0' || *endp == ' ') {
        *out = val_integer((int)lv);
    } else {
        *out = val_double(atof(s));
    }
    return ERR_NONE;
}

static error_t fn_left(value_t *args, int nargs, value_t *out) {
    if (nargs != 2) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    int n;
    EVAL_CHECK(val_to_integer(&args[1], &n));
    if (n < 0) return ERR_ILLEGAL_FUNCTION_CALL;
    int len = args[0].sval->len;
    if (n > len) n = len;
    *out = val_string(args[0].sval->data, n);
    return ERR_NONE;
}

static error_t fn_right(value_t *args, int nargs, value_t *out) {
    if (nargs != 2) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    int n;
    EVAL_CHECK(val_to_integer(&args[1], &n));
    if (n < 0) return ERR_ILLEGAL_FUNCTION_CALL;
    int len = args[0].sval->len;
    if (n > len) n = len;
    *out = val_string(args[0].sval->data + len - n, n);
    return ERR_NONE;
}

static error_t fn_mid(value_t *args, int nargs, value_t *out) {
    if (nargs < 2 || nargs > 3) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    int start;
    EVAL_CHECK(val_to_integer(&args[1], &start));
    start--;
    if (start < 0) start = 0;
    int len = args[0].sval->len;
    int count = len - start;
    if (nargs == 3) {
        EVAL_CHECK(val_to_integer(&args[2], &count));
        if (count < 0) return ERR_ILLEGAL_FUNCTION_CALL;
    }
    if (start >= len) {
        *out = val_string_cstr("");
        return ERR_NONE;
    }
    if (start + count > len) count = len - start;
    *out = val_string(args[0].sval->data + start, count);
    return ERR_NONE;
}

/* INSTR([start,] str, search) - find substring */
static error_t fn_instr(value_t *args, int nargs, value_t *out) {
    int start = 1;
    const char *haystack, *needle;
    int hlen;
    if (nargs == 2) {
        if (args[0].type != VAL_STRING || args[1].type != VAL_STRING)
            return ERR_TYPE_MISMATCH;
        haystack = args[0].sval->data;
        hlen = args[0].sval->len;
        needle = args[1].sval->data;
    } else if (nargs == 3) {
        EVAL_CHECK(val_to_integer(&args[0], &start));
        if (args[1].type != VAL_STRING || args[2].type != VAL_STRING)
            return ERR_TYPE_MISMATCH;
        haystack = args[1].sval->data;
        hlen = args[1].sval->len;
        needle = args[2].sval->data;
    } else {
        return ERR_ILLEGAL_FUNCTION_CALL;
    }
    if (start < 1) return ERR_ILLEGAL_FUNCTION_CALL;
    start--;
    if (start >= hlen || needle[0] == '\0') {
        *out = val_integer(0);
        return ERR_NONE;
    }
    char *p = strstr(haystack + start, needle);
    if (p)
        *out = val_integer((int)(p - haystack) + 1);
    else
        *out = val_integer(0);
    return ERR_NONE;
}

/* INSTRREV(haystack$, needle$ [, start]) - search from right */
static error_t fn_instrrev(value_t *args, int nargs, value_t *out) {
    const char *haystack, *needle;
    int hlen, nlen, start;
    if (nargs == 2) {
        if (args[0].type != VAL_STRING || args[1].type != VAL_STRING)
            return ERR_TYPE_MISMATCH;
        haystack = args[0].sval->data;
        hlen = args[0].sval->len;
        needle = args[1].sval->data;
        nlen = args[1].sval->len;
        start = hlen;
    } else if (nargs == 3) {
        if (args[0].type != VAL_STRING || args[1].type != VAL_STRING)
            return ERR_TYPE_MISMATCH;
        haystack = args[0].sval->data;
        hlen = args[0].sval->len;
        needle = args[1].sval->data;
        nlen = args[1].sval->len;
        EVAL_CHECK(val_to_integer(&args[2], &start));
    } else {
        return ERR_ILLEGAL_FUNCTION_CALL;
    }
    if (nlen == 0 || start < 1) { *out = val_integer(0); return ERR_NONE; }
    /* Search backwards from start position */
    int last = start - 1;
    if (last + nlen > hlen) last = hlen - nlen;
    for (int i = last; i >= 0; i--) {
        if (memcmp(haystack + i, needle, nlen) == 0) {
            *out = val_integer(i + 1);
            return ERR_NONE;
        }
    }
    *out = val_integer(0);
    return ERR_NONE;
}

/* UCASE$(str) */
static error_t fn_ucase(value_t *args, int nargs, value_t *out) {
    if (nargs != 1 || args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    int len = args[0].sval->len;
    char *buf = malloc(len + 1);
    if (!buf) return ERR_OUT_OF_MEMORY;
    for (int i = 0; i < len; i++)
        buf[i] = toupper((unsigned char)args[0].sval->data[i]);
    buf[len] = '\0';
    *out = val_string(buf, len);
    free(buf);
    return ERR_NONE;
}

/* LCASE$(str) */
static error_t fn_lcase(value_t *args, int nargs, value_t *out) {
    if (nargs != 1 || args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    int len = args[0].sval->len;
    char *buf = malloc(len + 1);
    if (!buf) return ERR_OUT_OF_MEMORY;
    for (int i = 0; i < len; i++)
        buf[i] = tolower((unsigned char)args[0].sval->data[i]);
    buf[len] = '\0';
    *out = val_string(buf, len);
    free(buf);
    return ERR_NONE;
}

/* LTRIM$(str) */
static error_t fn_ltrim(value_t *args, int nargs, value_t *out) {
    if (nargs != 1 || args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    const char *s = args[0].sval->data;
    int len = args[0].sval->len;
    int i = 0;
    while (i < len && s[i] == ' ') i++;
    *out = val_string(s + i, len - i);
    return ERR_NONE;
}

/* RTRIM$(str) */
static error_t fn_rtrim(value_t *args, int nargs, value_t *out) {
    if (nargs != 1 || args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    const char *s = args[0].sval->data;
    int len = args[0].sval->len;
    while (len > 0 && s[len - 1] == ' ') len--;
    *out = val_string(s, len);
    return ERR_NONE;
}

/* TRIM$(str) */
static error_t fn_trim(value_t *args, int nargs, value_t *out) {
    if (nargs != 1 || args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    const char *s = args[0].sval->data;
    int len = args[0].sval->len;
    int i = 0;
    while (i < len && s[i] == ' ') i++;
    while (len > i && s[len - 1] == ' ') len--;
    *out = val_string(s + i, len - i);
    return ERR_NONE;
}

/* SPACE$(n) - return n spaces */
static error_t fn_space(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int n;
    EVAL_CHECK(val_to_integer(&args[0], &n));
    if (n < 0) return ERR_ILLEGAL_FUNCTION_CALL;
    if (n > MAX_STRING_LEN) n = MAX_STRING_LEN;
    char *buf = malloc(n + 1);
    if (!buf) return ERR_OUT_OF_MEMORY;
    memset(buf, ' ', n);
    buf[n] = '\0';
    *out = val_string(buf, n);
    free(buf);
    return ERR_NONE;
}

/* STRING$(n, char_or_code) - repeat a character */
static error_t fn_string(value_t *args, int nargs, value_t *out) {
    if (nargs != 2) return ERR_ILLEGAL_FUNCTION_CALL;
    int n;
    EVAL_CHECK(val_to_integer(&args[0], &n));
    if (n < 0) return ERR_ILLEGAL_FUNCTION_CALL;
    if (n > MAX_STRING_LEN) n = MAX_STRING_LEN;
    char ch;
    if (args[1].type == VAL_STRING) {
        if (args[1].sval->len == 0) return ERR_ILLEGAL_FUNCTION_CALL;
        ch = args[1].sval->data[0];
    } else {
        int code;
        EVAL_CHECK(val_to_integer(&args[1], &code));
        if (code < 0 || code > 255) return ERR_ILLEGAL_FUNCTION_CALL;
        ch = (char)code;
    }
    char *buf = malloc(n + 1);
    if (!buf) return ERR_OUT_OF_MEMORY;
    memset(buf, ch, n);
    buf[n] = '\0';
    *out = val_string(buf, n);
    free(buf);
    return ERR_NONE;
}

/* MKI$(n) - pack integer into 2-byte string (little-endian) */
static error_t fn_mki(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int v; EVAL_CHECK(val_to_integer(&args[0], &v));
    char buf[2];
    buf[0] = (char)(v & 0xFF);
    buf[1] = (char)((v >> 8) & 0xFF);
    *out = val_string(buf, 2);
    return ERR_NONE;
}

/* MKL$(n) - pack long integer into 4-byte string (little-endian) */
static error_t fn_mkl(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int v; EVAL_CHECK(val_to_integer(&args[0], &v));
    char buf[4];
    buf[0] = (char)(v & 0xFF);
    buf[1] = (char)((v >> 8) & 0xFF);
    buf[2] = (char)((v >> 16) & 0xFF);
    buf[3] = (char)((v >> 24) & 0xFF);
    *out = val_string(buf, 4);
    return ERR_NONE;
}

/* MKD$(n) - pack double into 8-byte string */
static error_t fn_mkd(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    char buf[8];
    memcpy(buf, &v, 8);
    *out = val_string(buf, 8);
    return ERR_NONE;
}

/* MKS$(n) - pack single-precision float into 4-byte string (little-endian) */
static error_t fn_mks(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    float f = (float)v;
    char buf[4];
    memcpy(buf, &f, 4);
    *out = val_string(buf, 4);
    return ERR_NONE;
}

/* CVS(s$) - unpack 4-byte string to single-precision float */
static error_t fn_cvs(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    if (!args[0].sval || args[0].sval->len < 4) return ERR_ILLEGAL_FUNCTION_CALL;
    float f;
    memcpy(&f, args[0].sval->data, 4);
    *out = val_double((double)f);
    return ERR_NONE;
}

/* CVI(s$) - unpack 2-byte string to integer (little-endian) */
static error_t fn_cvi(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    if (!args[0].sval || args[0].sval->len < 2) return ERR_ILLEGAL_FUNCTION_CALL;
    const unsigned char *p = (const unsigned char *)args[0].sval->data;
    int v = (int)(short)(p[0] | (p[1] << 8));
    *out = val_integer(v);
    return ERR_NONE;
}

/* CVL(s$) - unpack 4-byte string to long integer (little-endian) */
static error_t fn_cvl(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    if (!args[0].sval || args[0].sval->len < 4) return ERR_ILLEGAL_FUNCTION_CALL;
    const unsigned char *p = (const unsigned char *)args[0].sval->data;
    int v = (int)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    *out = val_integer(v);
    return ERR_NONE;
}

/* CVD(s$) - unpack 8-byte string to double */
static error_t fn_cvd(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    if (!args[0].sval || args[0].sval->len < 8) return ERR_ILLEGAL_FUNCTION_CALL;
    double v;
    memcpy(&v, args[0].sval->data, 8);
    *out = val_double(v);
    return ERR_NONE;
}

/* CURDIR$() - return current working directory */
static error_t fn_curdir(value_t *args, int nargs, value_t *out) {
    if (nargs != 0) return ERR_ILLEGAL_FUNCTION_CALL;
    char buf[256];
    if (!getcwd(buf, sizeof(buf))) {
        *out = val_string_cstr("");
        return ERR_NONE;
    }
    *out = val_string_cstr(buf);
    return ERR_NONE;
}

/* PEEK/POKE simulated memory state */
extern unsigned char peek_poke_mem[];
int def_seg = 0;  /* DEF SEG segment (address = seg*16 + offset) */
int console_width = 80; /* WIDTH setting */
#define PEEK_POKE_SIZE 65536

/* PEEK(addr) - read byte from simulated memory (uses DEF SEG) */
static error_t fn_peek(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int offset;
    EVAL_CHECK(val_to_integer(&args[0], &offset));
    int addr = def_seg * 16 + offset;
    if (addr < 0 || addr >= PEEK_POKE_SIZE) return ERR_ILLEGAL_FUNCTION_CALL;
    *out = val_integer((int)peek_poke_mem[addr]);
    return ERR_NONE;
}

/* STRIG(n) - joystick trigger state (stub, returns 0) */
static error_t fn_strig(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    (void)args;
    *out = val_integer(0);
    return ERR_NONE;
}

/* STICK(n) - joystick position (stub, returns 0) */
static error_t fn_stick(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    (void)args;
    *out = val_integer(0);
    return ERR_NONE;
}

/* SEEK(n) - return current file position (1-based) */
static error_t fn_seek(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int handle;
    EVAL_CHECK(val_to_integer(&args[0], &handle));
    FILE *fp = fileio_get(handle);
    if (!fp) { *out = val_integer(0); return ERR_NONE; }
    long pos = ftell(fp);
    *out = val_integer(pos < 0 ? 1 : (int)(pos + 1));
    return ERR_NONE;
}

/* INP(port) - hardware port read (stub, returns 0) */
static error_t fn_inp(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    (void)args;
    *out = val_integer(0);
    return ERR_NONE;
}

/* VARPTR(var) - return dummy address for variable */
static error_t fn_varptr(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    (void)args;
    *out = val_integer(0);
    return ERR_NONE;
}

/* SADD(str$) - return dummy string address */
static error_t fn_sadd(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    (void)args;
    *out = val_integer(0);
    return ERR_NONE;
}

/* LPOS(n) - printer column position (stub, returns 0) */
static error_t fn_lpos(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    (void)args;
    *out = val_integer(0);
    return ERR_NONE;
}

/* FRE(x) - return free memory (QBasic compat, returns constant) */
static error_t fn_fre(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    (void)args; /* argument ignored per QBasic convention */
    *out = val_integer(65536);
    return ERR_NONE;
}

/* FILEEXISTS(path$) - return -1 (true) if file exists, 0 (false) otherwise */
static error_t fn_fileexists(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    const char *path = args[0].sval ? args[0].sval->data : "";
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        *out = val_integer(-1); /* true */
    } else {
        *out = val_integer(0);  /* false */
    }
    return ERR_NONE;
}

/* HEX$(n) - hexadecimal string */
static error_t fn_hex(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int v;
    EVAL_CHECK(val_to_integer(&args[0], &v));
    char buf[16];
    /* Print as unsigned hex */
    unsigned int uv = (unsigned int)v;
    int pos = 0;
    if (uv == 0) {
        buf[pos++] = '0';
    } else {
        char tmp[16];
        int tpos = 0;
        while (uv > 0) {
            int d = uv & 0xF;
            tmp[tpos++] = d < 10 ? '0' + d : 'A' + d - 10;
            uv >>= 4;
        }
        while (tpos > 0) buf[pos++] = tmp[--tpos];
    }
    buf[pos] = '\0';
    *out = val_string_cstr(buf);
    return ERR_NONE;
}

/* OCT$(n) - octal string */
static error_t fn_oct(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int v;
    EVAL_CHECK(val_to_integer(&args[0], &v));
    char buf[16];
    unsigned int uv = (unsigned int)v;
    int pos = 0;
    if (uv == 0) {
        buf[pos++] = '0';
    } else {
        char tmp[16];
        int tpos = 0;
        while (uv > 0) {
            tmp[tpos++] = '0' + (uv & 7);
            uv >>= 3;
        }
        while (tpos > 0) buf[pos++] = tmp[--tpos];
    }
    buf[pos] = '\0';
    *out = val_string_cstr(buf);
    return ERR_NONE;
}

/* BIN$(n) - binary string */
static error_t fn_bin(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int v;
    EVAL_CHECK(val_to_integer(&args[0], &v));
    char buf[33];
    unsigned int uv = (unsigned int)v;
    int pos = 0;
    if (uv == 0) {
        buf[pos++] = '0';
    } else {
        char tmp[33];
        int tpos = 0;
        while (uv > 0) {
            tmp[tpos++] = '0' + (uv & 1);
            uv >>= 1;
        }
        while (tpos > 0) buf[pos++] = tmp[--tpos];
    }
    buf[pos] = '\0';
    *out = val_string_cstr(buf);
    return ERR_NONE;
}

/* REPLACE$(str, find, replacement) */
static error_t fn_replace(value_t *args, int nargs, value_t *out) {
    if (nargs != 3) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING || args[1].type != VAL_STRING ||
        args[2].type != VAL_STRING)
        return ERR_TYPE_MISMATCH;
    const char *src = args[0].sval->data;
    int slen = args[0].sval->len;
    const char *find = args[1].sval->data;
    int flen = args[1].sval->len;
    const char *rep = args[2].sval->data;
    int rlen = args[2].sval->len;
    if (flen == 0) {
        *out = val_copy(&args[0]);
        return ERR_NONE;
    }
    /* Build result */
    char buf[MAX_STRING_LEN + 1];
    int pos = 0, i = 0;
    while (i <= slen - flen && pos < MAX_STRING_LEN) {
        if (memcmp(src + i, find, flen) == 0) {
            int copy = rlen;
            if (pos + copy > MAX_STRING_LEN) copy = MAX_STRING_LEN - pos;
            memcpy(buf + pos, rep, copy);
            pos += copy;
            i += flen;
        } else {
            buf[pos++] = src[i++];
        }
    }
    /* Copy remainder */
    while (i < slen && pos < MAX_STRING_LEN) buf[pos++] = src[i++];
    buf[pos] = '\0';
    *out = val_string(buf, pos);
    return ERR_NONE;
}

/* TIMER - seconds since midnight */
static error_t fn_timer(value_t *args, int nargs, value_t *out) {
    if (nargs != 0) return ERR_ILLEGAL_FUNCTION_CALL;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm) { *out = val_integer(0); return ERR_NONE; }
    *out = val_double((double)(tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec));
    return ERR_NONE;
}

/* DATE$ - return current date as "MM/DD/YYYY" */
static error_t fn_date(value_t *args, int nargs, value_t *out) {
    if (nargs != 0) return ERR_ILLEGAL_FUNCTION_CALL;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm) { *out = val_string_cstr(""); return ERR_NONE; }
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d",
             tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900);
    *out = val_string_cstr(buf);
    return ERR_NONE;
}

/* TIME$ - return current time as "HH:MM:SS" */
static error_t fn_time_str(value_t *args, int nargs, value_t *out) {
    if (nargs != 0) return ERR_ILLEGAL_FUNCTION_CALL;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm) { *out = val_string_cstr(""); return ERR_NONE; }
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    *out = val_string_cstr(buf);
    return ERR_NONE;
}

/* TAB(n) - return spaces to reach absolute column n (1-based) */
static error_t fn_tab(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int n;
    EVAL_CHECK(val_to_integer(&args[0], &n));
    n--;  /* convert 1-based column to 0-based */
    int spaces = n - *active_print_col;
    if (spaces < 0) spaces = 0;
    if (spaces > MAX_STRING_LEN) spaces = MAX_STRING_LEN;
    char *buf = malloc(spaces + 1);
    if (!buf) return ERR_OUT_OF_MEMORY;
    memset(buf, ' ', spaces);
    buf[spaces] = '\0';
    *out = val_string(buf, spaces);
    free(buf);
    return ERR_NONE;
}

/* LOC(n) - current byte position in file */
static error_t fn_loc(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int handle;
    EVAL_CHECK(val_to_integer(&args[0], &handle));
    FILE *fp = fileio_get(handle);
    if (!fp) { *out = val_integer(0); return ERR_NONE; }
    long pos = ftell(fp);
    *out = val_integer(pos < 0 ? 0 : (int)pos);
    return ERR_NONE;
}

/* LOF(n) - length of file in bytes */
static error_t fn_lof(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int handle;
    EVAL_CHECK(val_to_integer(&args[0], &handle));
    FILE *fp = fileio_get(handle);
    if (!fp) { *out = val_integer(0); return ERR_NONE; }
    long saved = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, saved, SEEK_SET);
    *out = val_integer(size < 0 ? 0 : (int)size);
    return ERR_NONE;
}

/* CSRLIN - current cursor row (1-based) */
static error_t fn_csrlin(value_t *args, int nargs, value_t *out) {
    (void)args;
    if (nargs != 0) return ERR_ILLEGAL_FUNCTION_CALL;
    *out = val_integer(print_row);
    return ERR_NONE;
}

/* POS(0) - current cursor column (1-based) */
static error_t fn_pos(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    (void)args; /* argument ignored, QBasic convention */
    *out = val_integer(print_col + 1);
    return ERR_NONE;
}

/* INPUT$(n [, #handle]) - read n characters from file or stdin */
static error_t fn_input_str(value_t *args, int nargs, value_t *out) {
    if (nargs < 1 || nargs > 2) return ERR_ILLEGAL_FUNCTION_CALL;
    int n;
    EVAL_CHECK(val_to_integer(&args[0], &n));
    if (n < 0) return ERR_ILLEGAL_FUNCTION_CALL;
    if (n > MAX_STRING_LEN) n = MAX_STRING_LEN;
    char *buf = malloc(n + 1);
    if (!buf) return ERR_OUT_OF_MEMORY;
    int count = 0;
    if (nargs == 2) {
        int handle;
        EVAL_CHECK(val_to_integer(&args[1], &handle));
        FILE *fp = fileio_get(handle);
        if (!fp) { free(buf); return ERR_BAD_FILE_NUMBER; }
        while (count < n) {
            int ch = fgetc(fp);
            if (ch == EOF) break;
            buf[count++] = (char)ch;
        }
    } else {
        while (count < n) {
            int ch = getchar();
            if (ch == EOF) break;
            buf[count++] = (char)ch;
        }
    }
    buf[count] = '\0';
    *out = val_string(buf, count);
    free(buf);
    return ERR_NONE;
}

/* EOF(n) - check end of file: returns -1 (true) or 0 (false) */
static error_t fn_eof(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int handle;
    EVAL_CHECK(val_to_integer(&args[0], &handle));
    *out = val_integer(fileio_eof(handle));
    return ERR_NONE;
}

/* FREEFILE - next available file handle */
static error_t fn_freefile(value_t *args, int nargs, value_t *out) {
    if (nargs != 0) return ERR_ILLEGAL_FUNCTION_CALL;
    *out = val_integer(fileio_freefile());
    return ERR_NONE;
}

/* SPC(n) - return n spaces */
static error_t fn_spc(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    int n;
    EVAL_CHECK(val_to_integer(&args[0], &n));
    if (n < 0) n = 0;
    if (n > MAX_STRING_LEN) n = MAX_STRING_LEN;
    char *buf = malloc(n + 1);
    if (!buf) return ERR_OUT_OF_MEMORY;
    memset(buf, ' ', n);
    buf[n] = '\0';
    *out = val_string(buf, n);
    free(buf);
    return ERR_NONE;
}

/* ================================================================
 * System integration builtins
 * ================================================================ */

/* ENVIRON$(name$) - get environment variable */
static error_t fn_environ(value_t *args, int nargs, value_t *out) {
    if (nargs != 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    const char *val = getenv(args[0].sval->data);
    if (val)
        *out = val_string_cstr(val);
    else
        *out = val_string_cstr("");
    return ERR_NONE;
}

/* COMMAND$([n]) - get command-line arguments (stub — no args support yet) */
static error_t fn_command(value_t *args, int nargs, value_t *out) {
    (void)args;
    if (nargs > 1) return ERR_ILLEGAL_FUNCTION_CALL;
    *out = val_string_cstr("");
    return ERR_NONE;
}

/* INKEY$ - non-blocking single character read */
static error_t fn_inkey(value_t *args, int nargs, value_t *out) {
    (void)args;
    if (nargs != 0) return ERR_ILLEGAL_FUNCTION_CALL;
    char ch[2];
    if (sb_term_inkey(ch)) {
        *out = val_string(ch, 1);
    } else {
        *out = val_string_cstr("");
    }
    return ERR_NONE;
}

/* DIR$([pattern$]) - directory listing with simple wildcard matching */
static DIR *dir_handle = NULL;
static char dir_pattern[256] = "";
static char dir_path[256] = ".";

/* Simple wildcard match: * matches any chars, ? matches one char */
static int wildmatch(const char *pattern, const char *str) {
    while (*pattern) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return 1;
            while (*str) {
                if (wildmatch(pattern, str)) return 1;
                str++;
            }
            return 0;
        } else if (*pattern == '?') {
            if (*str == '\0') return 0;
            pattern++; str++;
        } else {
            if (toupper((unsigned char)*pattern) != toupper((unsigned char)*str))
                return 0;
            pattern++; str++;
        }
    }
    return *str == '\0';
}

static error_t fn_dir(value_t *args, int nargs, value_t *out) {
    if (nargs > 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (nargs == 1) {
        /* DIR$(pattern$) - start new search */
        if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
        if (dir_handle) { closedir(dir_handle); dir_handle = NULL; }

        const char *spec = args[0].sval->data;
        /* Split into path and pattern at last / */
        const char *last_slash = NULL;
        for (const char *p = spec; *p; p++)
            if (*p == '/') last_slash = p;

        if (last_slash) {
            int plen = (int)(last_slash - spec);
            if (plen >= (int)sizeof(dir_path)) plen = sizeof(dir_path) - 1;
            memcpy(dir_path, spec, plen);
            dir_path[plen] = '\0';
            strncpy(dir_pattern, last_slash + 1, sizeof(dir_pattern) - 1);
            dir_pattern[sizeof(dir_pattern) - 1] = '\0';
        } else {
            strcpy(dir_path, ".");
            strncpy(dir_pattern, spec, sizeof(dir_pattern) - 1);
            dir_pattern[sizeof(dir_pattern) - 1] = '\0';
        }
        if (dir_pattern[0] == '\0') strcpy(dir_pattern, "*");

        dir_handle = opendir(dir_path);
        if (!dir_handle) {
            *out = val_string_cstr("");
            return ERR_NONE;
        }
    }
    /* Return next matching entry */
    if (!dir_handle) {
        *out = val_string_cstr("");
        return ERR_NONE;
    }
    struct dirent *ent;
    while ((ent = readdir(dir_handle)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (wildmatch(dir_pattern, ent->d_name)) {
            *out = val_string_cstr(ent->d_name);
            return ERR_NONE;
        }
    }
    /* End of directory */
    closedir(dir_handle);
    dir_handle = NULL;
    *out = val_string_cstr("");
    return ERR_NONE;
}

/* MIN(a, b) / MAX(a, b) */
static error_t fn_min(value_t *args, int nargs, value_t *out) {
    if (nargs < 2) return ERR_ILLEGAL_FUNCTION_CALL;
    double a, b;
    EVAL_CHECK(get_num(&args[0], &a));
    EVAL_CHECK(get_num(&args[1], &b));
    *out = val_double(a < b ? a : b);
    return ERR_NONE;
}

static error_t fn_max(value_t *args, int nargs, value_t *out) {
    if (nargs < 2) return ERR_ILLEGAL_FUNCTION_CALL;
    double a, b;
    EVAL_CHECK(get_num(&args[0], &a));
    EVAL_CHECK(get_num(&args[1], &b));
    *out = val_double(a > b ? a : b);
    return ERR_NONE;
}

/* FLOOR(x) / CEIL(x) */
static error_t fn_floor(value_t *args, int nargs, value_t *out) {
    if (nargs < 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    *out = val_double(floor(v));
    return ERR_NONE;
}

static error_t fn_ceil(value_t *args, int nargs, value_t *out) {
    if (nargs < 1) return ERR_ILLEGAL_FUNCTION_CALL;
    double v; EVAL_CHECK(get_num(&args[0], &v));
    /* ceil: smallest integer >= v */
    double f = floor(v);
    *out = val_double(v == f ? f : f + 1.0);
    return ERR_NONE;
}

/* REVERSE$(str) */
static error_t fn_reverse(value_t *args, int nargs, value_t *out) {
    if (nargs < 1) return ERR_ILLEGAL_FUNCTION_CALL;
    if (args[0].type != VAL_STRING) return ERR_TYPE_MISMATCH;
    const char *src = args[0].sval ? args[0].sval->data : "";
    int len = (int)strlen(src);
    char *buf = malloc(len + 1);
    if (!buf) return ERR_OUT_OF_MEMORY;
    for (int i = 0; i < len; i++)
        buf[i] = src[len - 1 - i];
    buf[len] = '\0';
    *out = val_string_cstr(buf);
    free(buf);
    return ERR_NONE;
}

/* ================================================================
 * RANDOMIZE support (called from eval, not dispatch table)
 * ================================================================ */

void builtin_randomize(int seed) {
    rng_state = (unsigned int)seed;
}

/* ================================================================
 * Dispatch table
 * ================================================================ */

typedef struct {
    const char *name;
    error_t (*fn)(value_t *args, int nargs, value_t *out);
} builtin_entry_t;

static const builtin_entry_t builtins[] = {
    /* Math */
    { "ABS",      fn_abs },
    { "INT",      fn_int },
    { "SGN",      fn_sgn },
    { "SQR",      fn_sqr },
    { "FIX",      fn_fix },
    { "CINT",     fn_cint },
    { "CLNG",     fn_cint },
    { "CDBL",     fn_cdbl },
    { "CSNG",     fn_cdbl },
    { "SIN",      fn_sin },
    { "COS",      fn_cos },
    { "TAN",      fn_tan },
    { "ATN",      fn_atn },
    { "ATN2",     fn_atn2 },
    { "ASIN",     fn_asin },
    { "ACOS",     fn_acos },
    { "SINH",     fn_sinh },
    { "COSH",     fn_cosh },
    { "TANH",     fn_tanh },
    { "ASINH",    fn_asinh },
    { "ACOSH",    fn_acosh },
    { "ATANH",    fn_atanh },
    { "EXP",      fn_exp },
    { "LOG",      fn_log },
    { "LOG10",    fn_log10 },
    { "LOG2",     fn_log2 },
    { "ROUND",    fn_round },
    { "MIN",      fn_min },
    { "MAX",      fn_max },
    { "FLOOR",    fn_floor },
    { "CEIL",     fn_ceil },
    { "RND",      fn_rnd },
    { "TIMER",    fn_timer },
    { "DATE$",    fn_date },
    { "TIME$",    fn_time_str },
    { "TAB",      fn_tab },
    { "SPC",      fn_spc },
    /* String */
    { "LEN",      fn_len },
    { "CHR$",     fn_chr },
    { "ASC",      fn_asc },
    { "STR$",     fn_str },
    { "VAL",      fn_val },
    { "LEFT$",    fn_left },
    { "RIGHT$",   fn_right },
    { "MID$",     fn_mid },
    { "INSTR",    fn_instr },
    { "INSTRREV", fn_instrrev },
    { "UCASE$",   fn_ucase },
    { "LCASE$",   fn_lcase },
    { "LTRIM$",   fn_ltrim },
    { "RTRIM$",   fn_rtrim },
    { "TRIM$",    fn_trim },
    { "SPACE$",   fn_space },
    { "STRING$",  fn_string },
    { "HEX$",     fn_hex },
    { "OCT$",     fn_oct },
    { "BIN$",     fn_bin },
    { "REPLACE$", fn_replace },
    { "REVERSE$", fn_reverse },
    /* Binary packing */
    { "MKI$",     fn_mki },
    { "MKL$",     fn_mkl },
    { "MKS$",     fn_mks },
    { "MKD$",     fn_mkd },
    { "CVI",      fn_cvi },
    { "CVL",      fn_cvl },
    { "CVS",      fn_cvs },
    { "CVD",      fn_cvd },
    /* File I/O */
    { "EOF",      fn_eof },
    { "FREEFILE", fn_freefile },
    { "LOC",      fn_loc },
    { "LOF",      fn_lof },
    { "CSRLIN",   fn_csrlin },
    { "POS",      fn_pos },
    { "INPUT$",   fn_input_str },
    /* System */
    { "ENVIRON$", fn_environ },
    { "COMMAND$", fn_command },
    { "INKEY$",   fn_inkey },
    { "DIR$",     fn_dir },
    { "CURDIR$",  fn_curdir },
    { "FILEEXISTS", fn_fileexists },
    { "FRE",      fn_fre },
    { "LPOS",     fn_lpos },
    { "INP",      fn_inp },
    { "VARPTR",   fn_varptr },
    { "SADD",     fn_sadd },
    { "SEEK",     fn_seek },
    { "PEEK",     fn_peek },
    { "STRIG",    fn_strig },
    { "STICK",    fn_stick },
    { NULL, NULL }
};

int builtin_exists(const char *name) {
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(builtins[i].name, name) == 0)
            return 1;
    }
    return 0;
}

error_t builtin_call(const char *name, value_t *args, int nargs, value_t *out) {
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(builtins[i].name, name) == 0)
            return builtins[i].fn(args, nargs, out);
    }
    return ERR_ILLEGAL_FUNCTION_CALL;
}
