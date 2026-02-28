#ifndef SBASIC_VALUE_H
#define SBASIC_VALUE_H

#include "error.h"

/* Maximum string length */
#define MAX_STRING_LEN 4096

/* Value types */
typedef enum {
    VAL_INTEGER,
    VAL_DOUBLE,
    VAL_STRING,
    VAL_RECORD
} val_type_t;

/* Reference-counted string */
typedef struct sb_string {
    int refcount;
    int len;
    char data[1]; /* flexible array */
} sb_string_t;

/* Forward-declare sb_record_t (defined after value_t) */
typedef struct sb_record sb_record_t;

/* Value union */
typedef struct value {
    val_type_t type;
    union {
        int ival;
        double dval;
        sb_string_t *sval;
        sb_record_t *rval;
    };
} value_t;

/* Reference-counted record (TYPE instance) */
struct sb_record {
    int refcount;
    int type_idx;    /* index into type_defs[] */
    int nfields;
    value_t fields[32];
};

/* String allocation / refcounting */
sb_string_t *string_alloc(const char *s, int len);
sb_string_t *string_alloc_cstr(const char *s);
void string_ref(sb_string_t *s);
void string_unref(sb_string_t *s);

/* Record allocation / refcounting */
sb_record_t *record_alloc(int type_idx, int nfields);
void record_ref(sb_record_t *r);
void record_unref(sb_record_t *r);

/* Value constructors */
value_t val_integer(int v);
value_t val_double(double v);
value_t val_string(const char *s, int len);
value_t val_string_cstr(const char *s);
value_t val_default(val_type_t type);

/* Value destruction (unrefs strings) */
void val_clear(value_t *v);

/* Copy a value (refs strings) */
value_t val_copy(const value_t *v);

/* Assign src to dst, clearing old dst */
void val_assign(value_t *dst, const value_t *src);

/* Type coercion */
error_t val_to_integer(const value_t *v, int *out);
error_t val_to_double(const value_t *v, double *out);
error_t val_to_string(const value_t *v, char *buf, int bufsize);

/* Get the C string pointer from a string value (or "" for non-string) */
const char *val_cstr(const value_t *v);

/* Arithmetic on values */
error_t val_add(const value_t *a, const value_t *b, value_t *result);
error_t val_sub(const value_t *a, const value_t *b, value_t *result);
error_t val_mul(const value_t *a, const value_t *b, value_t *result);
error_t val_div(const value_t *a, const value_t *b, value_t *result);
error_t val_idiv(const value_t *a, const value_t *b, value_t *result);
error_t val_mod(const value_t *a, const value_t *b, value_t *result);
error_t val_pow(const value_t *a, const value_t *b, value_t *result);
error_t val_neg(const value_t *a, value_t *result);

/* Comparison: sets *out to -1, 0, or 1 */
error_t val_compare(const value_t *a, const value_t *b, int *out);

/* Truthiness: 0 and "" are false, everything else true */
int val_is_true(const value_t *v);

/* Determine the type suffix implies */
val_type_t type_from_suffix(char suffix);

#endif
