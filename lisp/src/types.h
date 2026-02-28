#ifndef LISP_TYPES_H
#define LISP_TYPES_H

#include <stdint.h>

/* Tagged 32-bit value:
 *   bit 0 = 1 → fixnum (31-bit signed, val >> 1)
 *   bit 0 = 0 → object pointer (8-byte aligned) or NIL (0)
 */
typedef int32_t val_t;

#define NIL          ((val_t)0)
#define IS_FIXNUM(v) ((v) & 1)
#define FIXNUM(n)    (((val_t)(n) << 1) | 1)
#define UNFIXNUM(v)  ((v) >> 1)
#define IS_NIL(v)    ((v) == NIL)
#define IS_PTR(v)    (!IS_FIXNUM(v) && (v) != NIL)
#define AS_OBJ(v)    ((object_t *)(v))
#define CAR(v)       (AS_OBJ(v)->cons.car)
#define CDR(v)       (AS_OBJ(v)->cons.cdr)

/* VOID sentinel: REPL suppresses printing for define/set! */
#define VOID_VAL     FIXNUM(0x3FFFFFFF)
#define IS_VOID(v)   ((v) == VOID_VAL)

/* Object types */
enum {
    OBJ_CONS,
    OBJ_SYMBOL,
    OBJ_STRING,
    OBJ_LAMBDA,
    OBJ_BUILTIN
};

typedef struct object {
    int type;
    int mark;
    struct object *gc_next;
    union {
        struct { val_t car, cdr; } cons;
        struct { char *name; } symbol;
        struct { char *data; int len; } string;
        struct { val_t params, body, env; } lambda;
        struct { const char *name; val_t (*fn)(val_t); } builtin;
    };
} object_t;

/* Error handling */
extern int g_error;
extern char g_errmsg[256];

#define CHECK_ERR if (g_error) return NIL

void lisp_error(const char *msg);
void lisp_error2(const char *msg, const char *detail);

#endif
