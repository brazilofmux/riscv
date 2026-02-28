#include "value.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* --- String refcounting --- */

sb_string_t *string_alloc(const char *s, int len) {
    sb_string_t *str = malloc(sizeof(sb_string_t) + len);
    if (!str) return NULL;
    str->refcount = 1;
    str->len = len;
    if (s)
        memcpy(str->data, s, len);
    str->data[len] = '\0';
    return str;
}

sb_string_t *string_alloc_cstr(const char *s) {
    return string_alloc(s, s ? (int)strlen(s) : 0);
}

void string_ref(sb_string_t *s) {
    if (s) s->refcount++;
}

void string_unref(sb_string_t *s) {
    if (s) {
        s->refcount--;
        if (s->refcount <= 0)
            free(s);
    }
}

/* --- Record refcounting --- */

sb_record_t *record_alloc(int type_idx, int nfields) {
    sb_record_t *r = malloc(sizeof(sb_record_t));
    if (!r) return NULL;
    r->refcount = 1;
    r->type_idx = type_idx;
    r->nfields = nfields;
    for (int i = 0; i < nfields; i++) {
        r->fields[i].type = VAL_INTEGER;
        r->fields[i].ival = 0;
    }
    return r;
}

void record_ref(sb_record_t *r) {
    if (r) r->refcount++;
}

void record_unref(sb_record_t *r) {
    if (r) {
        r->refcount--;
        if (r->refcount <= 0) {
            for (int i = 0; i < r->nfields; i++)
                val_clear(&r->fields[i]);
            free(r);
        }
    }
}

/* --- Value constructors --- */

value_t val_integer(int v) {
    value_t r;
    r.type = VAL_INTEGER;
    r.ival = v;
    return r;
}

value_t val_double(double v) {
    value_t r;
    r.type = VAL_DOUBLE;
    r.dval = v;
    return r;
}

value_t val_string(const char *s, int len) {
    value_t r;
    r.type = VAL_STRING;
    r.sval = string_alloc(s, len);
    return r;
}

value_t val_string_cstr(const char *s) {
    value_t r;
    r.type = VAL_STRING;
    r.sval = string_alloc_cstr(s ? s : "");
    return r;
}

value_t val_default(val_type_t type) {
    switch (type) {
        case VAL_INTEGER: return val_integer(0);
        case VAL_DOUBLE:  return val_double(0.0);
        case VAL_STRING:  return val_string_cstr("");
        case VAL_RECORD:  return val_integer(0); /* records created via record_alloc */
    }
    return val_integer(0);
}

/* --- Value lifecycle --- */

void val_clear(value_t *v) {
    if (v->type == VAL_STRING)
        string_unref(v->sval);
    else if (v->type == VAL_RECORD)
        record_unref(v->rval);
    v->type = VAL_INTEGER;
    v->ival = 0;
}

value_t val_copy(const value_t *v) {
    value_t r = *v;
    if (r.type == VAL_STRING)
        string_ref(r.sval);
    else if (r.type == VAL_RECORD)
        record_ref(r.rval);
    return r;
}

void val_assign(value_t *dst, const value_t *src) {
    if (src->type == VAL_STRING)
        string_ref(src->sval);
    else if (src->type == VAL_RECORD)
        record_ref(src->rval);
    if (dst->type == VAL_STRING)
        string_unref(dst->sval);
    else if (dst->type == VAL_RECORD)
        record_unref(dst->rval);
    *dst = *src;
}

/* --- Type coercion --- */

static double val_as_double(const value_t *v) {
    switch (v->type) {
        case VAL_INTEGER: return (double)v->ival;
        case VAL_DOUBLE:  return v->dval;
        case VAL_STRING:  return atof(v->sval->data);
        case VAL_RECORD:  return 0.0;
    }
    return 0.0;
}

static int val_as_int(const value_t *v) {
    switch (v->type) {
        case VAL_INTEGER: return v->ival;
        case VAL_DOUBLE:  return (int)v->dval;
        case VAL_STRING:  return atoi(v->sval->data);
        case VAL_RECORD:  return 0;
    }
    return 0;
}

error_t val_to_integer(const value_t *v, int *out) {
    if (v->type == VAL_STRING || v->type == VAL_RECORD)
        return ERR_TYPE_MISMATCH;
    if (v->type == VAL_DOUBLE) {
        double d = v->dval;
        if (d > 2147483647.0 || d < -2147483648.0)
            return ERR_OVERFLOW;
    }
    *out = val_as_int(v);
    return ERR_NONE;
}

error_t val_to_double(const value_t *v, double *out) {
    if (v->type == VAL_STRING || v->type == VAL_RECORD)
        return ERR_TYPE_MISMATCH;
    *out = val_as_double(v);
    return ERR_NONE;
}

error_t val_to_string(const value_t *v, char *buf, int bufsize) {
    switch (v->type) {
        case VAL_STRING:
            if (v->sval->len >= bufsize) return ERR_STRING_TOO_LONG;
            memcpy(buf, v->sval->data, v->sval->len + 1);
            break;
        case VAL_INTEGER:
            snprintf(buf, bufsize, "%d", v->ival);
            break;
        case VAL_DOUBLE:
            snprintf(buf, bufsize, "%g", v->dval);
            break;
        case VAL_RECORD:
            return ERR_TYPE_MISMATCH;
    }
    return ERR_NONE;
}

const char *val_cstr(const value_t *v) {
    if (v->type == VAL_STRING && v->sval)
        return v->sval->data;
    return "";
}

/* --- Arithmetic --- */

#define INT_RANGE(x) ((x) >= -2147483648LL && (x) <= 2147483647LL)

/* Promote two numeric values to a common type for arithmetic */
static int promote(const value_t *a, const value_t *b) {
    /* Returns 1 if either is double, 0 if both int */
    return (a->type == VAL_DOUBLE || b->type == VAL_DOUBLE);
}

error_t val_add(const value_t *a, const value_t *b, value_t *result) {
    if (a->type == VAL_RECORD || b->type == VAL_RECORD)
        return ERR_TYPE_MISMATCH;
    /* String concatenation */
    if (a->type == VAL_STRING && b->type == VAL_STRING) {
        int len = a->sval->len + b->sval->len;
        if (len > MAX_STRING_LEN) return ERR_STRING_TOO_LONG;
        sb_string_t *s = string_alloc(NULL, len);
        if (!s) return ERR_OUT_OF_MEMORY;
        memcpy(s->data, a->sval->data, a->sval->len);
        memcpy(s->data + a->sval->len, b->sval->data, b->sval->len);
        s->data[len] = '\0';
        result->type = VAL_STRING;
        result->sval = s;
        return ERR_NONE;
    }
    if (a->type == VAL_STRING || b->type == VAL_STRING)
        return ERR_TYPE_MISMATCH;
    if (promote(a, b)) {
        *result = val_double(val_as_double(a) + val_as_double(b));
    } else {
        long long r = (long long)a->ival + (long long)b->ival;
        if (INT_RANGE(r)) *result = val_integer((int)r);
        else *result = val_double((double)r);
    }
    return ERR_NONE;
}

error_t val_sub(const value_t *a, const value_t *b, value_t *result) {
    if (a->type == VAL_STRING || b->type == VAL_STRING ||
        a->type == VAL_RECORD || b->type == VAL_RECORD)
        return ERR_TYPE_MISMATCH;
    if (promote(a, b)) {
        *result = val_double(val_as_double(a) - val_as_double(b));
    } else {
        long long r = (long long)a->ival - (long long)b->ival;
        if (INT_RANGE(r)) *result = val_integer((int)r);
        else *result = val_double((double)r);
    }
    return ERR_NONE;
}

error_t val_mul(const value_t *a, const value_t *b, value_t *result) {
    if (a->type == VAL_STRING || b->type == VAL_STRING ||
        a->type == VAL_RECORD || b->type == VAL_RECORD)
        return ERR_TYPE_MISMATCH;
    if (promote(a, b)) {
        *result = val_double(val_as_double(a) * val_as_double(b));
    } else {
        long long r = (long long)a->ival * (long long)b->ival;
        if (INT_RANGE(r)) *result = val_integer((int)r);
        else *result = val_double((double)r);
    }
    return ERR_NONE;
}

error_t val_div(const value_t *a, const value_t *b, value_t *result) {
    if (a->type == VAL_STRING || b->type == VAL_STRING ||
        a->type == VAL_RECORD || b->type == VAL_RECORD)
        return ERR_TYPE_MISMATCH;
    double db = val_as_double(b);
    if (db == 0.0) return ERR_DIVISION_BY_ZERO;
    *result = val_double(val_as_double(a) / db);
    return ERR_NONE;
}

error_t val_idiv(const value_t *a, const value_t *b, value_t *result) {
    if (a->type == VAL_STRING || b->type == VAL_STRING ||
        a->type == VAL_RECORD || b->type == VAL_RECORD)
        return ERR_TYPE_MISMATCH;
    int ib = val_as_int(b);
    if (ib == 0) return ERR_DIVISION_BY_ZERO;
    *result = val_integer(val_as_int(a) / ib);
    return ERR_NONE;
}

error_t val_mod(const value_t *a, const value_t *b, value_t *result) {
    if (a->type == VAL_STRING || b->type == VAL_STRING ||
        a->type == VAL_RECORD || b->type == VAL_RECORD)
        return ERR_TYPE_MISMATCH;
    int ib = val_as_int(b);
    if (ib == 0) return ERR_DIVISION_BY_ZERO;
    *result = val_integer(val_as_int(a) % ib);
    return ERR_NONE;
}

error_t val_pow(const value_t *a, const value_t *b, value_t *result) {
    if (a->type == VAL_STRING || b->type == VAL_STRING ||
        a->type == VAL_RECORD || b->type == VAL_RECORD)
        return ERR_TYPE_MISMATCH;
    /* Integer power for integer^positive-integer */
    if (a->type == VAL_INTEGER && b->type == VAL_INTEGER && b->ival >= 0) {
        long long base = a->ival;
        int e = b->ival;
        long long r = 1;
        int overflow = 0;
        while (e > 0) {
            if (e & 1) {
                r *= base;
                if (!INT_RANGE(r)) { overflow = 1; break; }
            }
            e >>= 1;
            if (e > 0) {
                base *= base;
                if (!INT_RANGE(base)) { overflow = 1; break; }
            }
        }
        if (!overflow)
            *result = val_integer((int)r);
        else
            *result = val_double(pow(val_as_double(a), val_as_double(b)));
    } else {
        *result = val_double(pow(val_as_double(a), val_as_double(b)));
    }
    return ERR_NONE;
}

error_t val_neg(const value_t *a, value_t *result) {
    if (a->type == VAL_STRING || a->type == VAL_RECORD)
        return ERR_TYPE_MISMATCH;
    if (a->type == VAL_INTEGER) {
        if (a->ival == (-2147483647 - 1))
            *result = val_double(2147483648.0);
        else
            *result = val_integer(-a->ival);
    } else
        *result = val_double(-a->dval);
    return ERR_NONE;
}

/* --- Comparison --- */

error_t val_compare(const value_t *a, const value_t *b, int *out) {
    if (a->type == VAL_RECORD || b->type == VAL_RECORD)
        return ERR_TYPE_MISMATCH;
    if (a->type == VAL_STRING && b->type == VAL_STRING) {
        *out = strcmp(a->sval->data, b->sval->data);
        if (*out < 0) *out = -1;
        if (*out > 0) *out = 1;
        return ERR_NONE;
    }
    if (a->type == VAL_STRING || b->type == VAL_STRING)
        return ERR_TYPE_MISMATCH;
    double da = val_as_double(a);
    double db = val_as_double(b);
    if (da < db) *out = -1;
    else if (da > db) *out = 1;
    else *out = 0;
    return ERR_NONE;
}

int val_is_true(const value_t *v) {
    switch (v->type) {
        case VAL_INTEGER: return v->ival != 0;
        case VAL_DOUBLE:  return v->dval != 0.0;
        case VAL_STRING:  return v->sval && v->sval->len > 0;
        case VAL_RECORD:  return v->rval != NULL;
    }
    return 0;
}

val_type_t type_from_suffix(char suffix) {
    switch (suffix) {
        case '%': return VAL_INTEGER;
        case '#': return VAL_DOUBLE;
        case '$': return VAL_STRING;
        default:  return VAL_DOUBLE;
    }
}
