#include "heap.h"
#include <stdlib.h>
#include <string.h>

/* Error globals */
int g_error;
char g_errmsg[256];

void lisp_error(const char *msg) {
    g_error = 1;
    strncpy(g_errmsg, msg, 255);
    g_errmsg[255] = 0;
}

void lisp_error2(const char *msg, const char *detail) {
    g_error = 1;
    int n = 0;
    const char *p = msg;
    while (*p && n < 200) { g_errmsg[n++] = *p++; }
    g_errmsg[n++] = ':';
    g_errmsg[n++] = ' ';
    p = detail;
    while (*p && n < 255) { g_errmsg[n++] = *p++; }
    g_errmsg[n] = 0;
}

/* Root stack */
val_t *root_stack[ROOT_STACK_SIZE];
int root_sp;

void push_root_checked(val_t *p) {
    if (root_sp >= ROOT_STACK_SIZE) {
        /* Fatal: can't recover from root stack overflow */
        extern int printf(const char *, ...);
        extern void exit(int);
        printf("Fatal: GC root stack overflow (%d)\n", ROOT_STACK_SIZE);
        exit(1);
    }
    root_stack[root_sp++] = p;
}

/* GC state */
static object_t *gc_list;   /* head of all-objects list */
static int obj_count;
static int gc_threshold = 256;

/* Symbol intern table */
#define MAX_SYMBOLS 1024
static val_t sym_table[MAX_SYMBOLS];
static int sym_count;

val_t sym_true;

/* Allocate raw object, link into GC list */
static object_t *obj_new(int type) {
    if (obj_count >= gc_threshold) {
        gc_collect();
    }
    object_t *o = (object_t *)malloc(sizeof(object_t));
    if (!o) {
        lisp_error("out of memory");
        return NULL;
    }
    memset(o, 0, sizeof(object_t));
    o->type = type;
    o->mark = 0;
    o->gc_next = gc_list;
    gc_list = o;
    obj_count++;
    return o;
}

/* Mark stack for iterative marking */
#define MARK_STACK_SIZE 1024
static val_t mark_stack[MARK_STACK_SIZE];
static int mark_sp;

static void mark_push(val_t v) {
    if (!IS_PTR(v)) return;
    if (mark_sp >= MARK_STACK_SIZE) {
        extern int printf(const char *, ...);
        extern void exit(int);
        printf("Fatal: GC mark stack overflow (%d)\n", MARK_STACK_SIZE);
        exit(1);
    }
    mark_stack[mark_sp++] = v;
}

static void mark_val(val_t v) {
    mark_push(v);
    while (mark_sp > 0) {
        val_t cur = mark_stack[--mark_sp];
        if (!IS_PTR(cur)) continue;
        object_t *o = AS_OBJ(cur);
        if (o->mark) continue;
        o->mark = 1;
        switch (o->type) {
        case OBJ_CONS:
            mark_push(o->cons.car);
            mark_push(o->cons.cdr);
            break;
        case OBJ_LAMBDA:
            mark_push(o->lambda.params);
            mark_push(o->lambda.body);
            mark_push(o->lambda.env);
            break;
        case OBJ_SYMBOL:
        case OBJ_STRING:
        case OBJ_BUILTIN:
            break;
        }
    }
}

void gc_collect(void) {
    /* Mark roots */
    int i;
    for (i = 0; i < root_sp; i++) {
        mark_val(*root_stack[i]);
    }
    /* Mark interned symbols (always reachable) */
    for (i = 0; i < sym_count; i++) {
        mark_val(sym_table[i]);
    }

    /* Sweep */
    object_t **pp = &gc_list;
    while (*pp) {
        object_t *o = *pp;
        if (o->mark) {
            o->mark = 0;
            pp = &o->gc_next;
        } else {
            *pp = o->gc_next;
            if (o->type == OBJ_SYMBOL) free(o->symbol.name);
            else if (o->type == OBJ_STRING) free(o->string.data);
            free(o);
            obj_count--;
        }
    }

    gc_threshold = obj_count * 2;
    if (gc_threshold < 256) gc_threshold = 256;
}

void heap_init(void) {
    gc_list = NULL;
    obj_count = 0;
    gc_threshold = 256;
    root_sp = 0;
    sym_count = 0;
    mark_sp = 0;
    sym_true = symbol_intern("#t");
}

val_t cons_alloc(val_t car, val_t cdr) {
    /* Root car and cdr in case GC triggers */
    PUSH_ROOT(car);
    PUSH_ROOT(cdr);
    object_t *o = obj_new(OBJ_CONS);
    POP_ROOTS(2);
    if (!o) return NIL;
    o->cons.car = car;
    o->cons.cdr = cdr;
    return (val_t)o;
}

val_t symbol_intern(const char *name) {
    int i;
    for (i = 0; i < sym_count; i++) {
        object_t *o = AS_OBJ(sym_table[i]);
        if (strcmp(o->symbol.name, name) == 0) {
            return sym_table[i];
        }
    }
    if (sym_count >= MAX_SYMBOLS) {
        lisp_error("too many symbols");
        return NIL;
    }
    object_t *o = obj_new(OBJ_SYMBOL);
    if (!o) return NIL;
    o->symbol.name = (char *)malloc(strlen(name) + 1);
    if (!o->symbol.name) {
        lisp_error("out of memory");
        return NIL;
    }
    strcpy(o->symbol.name, name);
    val_t v = (val_t)o;
    sym_table[sym_count++] = v;
    return v;
}

val_t string_alloc(const char *data, int len) {
    object_t *o = obj_new(OBJ_STRING);
    if (!o) return NIL;
    o->string.data = (char *)malloc(len + 1);
    if (!o->string.data) {
        lisp_error("out of memory");
        return NIL;
    }
    memcpy(o->string.data, data, len);
    o->string.data[len] = 0;
    o->string.len = len;
    return (val_t)o;
}

val_t lambda_alloc(val_t params, val_t body, val_t env) {
    PUSH_ROOT(params);
    PUSH_ROOT(body);
    PUSH_ROOT(env);
    object_t *o = obj_new(OBJ_LAMBDA);
    POP_ROOTS(3);
    if (!o) return NIL;
    o->lambda.params = params;
    o->lambda.body = body;
    o->lambda.env = env;
    return (val_t)o;
}

val_t builtin_alloc(const char *name, val_t (*fn)(val_t)) {
    object_t *o = obj_new(OBJ_BUILTIN);
    if (!o) return NIL;
    o->builtin.name = name;
    o->builtin.fn = fn;
    return (val_t)o;
}
