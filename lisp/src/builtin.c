#include "builtin.h"
#include "heap.h"
#include "env.h"
#include "eval.h"
#include "print.h"
#include "reader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Helper: get list length */
static int args_len(val_t args) {
    int n = 0;
    while (!IS_NIL(args)) { n++; args = CDR(args); }
    return n;
}

/* Helper: get nth argument (0-based) */
static val_t arg_nth(val_t args, int n) {
    while (n > 0) { args = CDR(args); n--; }
    return CAR(args);
}

/* ---- Arithmetic ---- */

static val_t bi_add(val_t args) {
    int sum = 0;
    while (!IS_NIL(args)) {
        val_t v = CAR(args);
        if (!IS_FIXNUM(v)) { lisp_error("+: not a number"); return NIL; }
        sum += UNFIXNUM(v);
        args = CDR(args);
    }
    return FIXNUM(sum);
}

static val_t bi_sub(val_t args) {
    if (IS_NIL(args)) { lisp_error("-: need at least 1 arg"); return NIL; }
    val_t first = CAR(args);
    if (!IS_FIXNUM(first)) { lisp_error("-: not a number"); return NIL; }
    if (IS_NIL(CDR(args))) return FIXNUM(-UNFIXNUM(first));
    int result = UNFIXNUM(first);
    args = CDR(args);
    while (!IS_NIL(args)) {
        val_t v = CAR(args);
        if (!IS_FIXNUM(v)) { lisp_error("-: not a number"); return NIL; }
        result -= UNFIXNUM(v);
        args = CDR(args);
    }
    return FIXNUM(result);
}

static val_t bi_mul(val_t args) {
    int product = 1;
    while (!IS_NIL(args)) {
        val_t v = CAR(args);
        if (!IS_FIXNUM(v)) { lisp_error("*: not a number"); return NIL; }
        product *= UNFIXNUM(v);
        args = CDR(args);
    }
    return FIXNUM(product);
}

static val_t bi_div(val_t args) {
    if (args_len(args) < 2) { lisp_error("/: need 2+ args"); return NIL; }
    val_t first = CAR(args);
    if (!IS_FIXNUM(first)) { lisp_error("/: not a number"); return NIL; }
    int result = UNFIXNUM(first);
    args = CDR(args);
    while (!IS_NIL(args)) {
        val_t v = CAR(args);
        if (!IS_FIXNUM(v)) { lisp_error("/: not a number"); return NIL; }
        int d = UNFIXNUM(v);
        if (d == 0) { lisp_error("/: division by zero"); return NIL; }
        result /= d;
        args = CDR(args);
    }
    return FIXNUM(result);
}

static val_t bi_modulo(val_t args) {
    if (args_len(args) != 2) { lisp_error("modulo: need 2 args"); return NIL; }
    val_t a = CAR(args), b = CAR(CDR(args));
    if (!IS_FIXNUM(a) || !IS_FIXNUM(b)) { lisp_error("modulo: not a number"); return NIL; }
    int bv = UNFIXNUM(b);
    if (bv == 0) { lisp_error("modulo: division by zero"); return NIL; }
    int r = UNFIXNUM(a) % bv;
    /* Scheme modulo: result has sign of divisor */
    if (r != 0 && ((r < 0) != (bv < 0))) r += bv;
    return FIXNUM(r);
}

static val_t bi_abs(val_t args) {
    val_t v = CAR(args);
    if (!IS_FIXNUM(v)) { lisp_error("abs: not a number"); return NIL; }
    int n = UNFIXNUM(v);
    return FIXNUM(n < 0 ? -n : n);
}

static val_t bi_min(val_t args) {
    if (IS_NIL(args)) { lisp_error("min: need args"); return NIL; }
    val_t v = CAR(args);
    if (!IS_FIXNUM(v)) { lisp_error("min: not a number"); return NIL; }
    int result = UNFIXNUM(v);
    args = CDR(args);
    while (!IS_NIL(args)) {
        v = CAR(args);
        if (!IS_FIXNUM(v)) { lisp_error("min: not a number"); return NIL; }
        int n = UNFIXNUM(v);
        if (n < result) result = n;
        args = CDR(args);
    }
    return FIXNUM(result);
}

static val_t bi_max(val_t args) {
    if (IS_NIL(args)) { lisp_error("max: need args"); return NIL; }
    val_t v = CAR(args);
    if (!IS_FIXNUM(v)) { lisp_error("max: not a number"); return NIL; }
    int result = UNFIXNUM(v);
    args = CDR(args);
    while (!IS_NIL(args)) {
        v = CAR(args);
        if (!IS_FIXNUM(v)) { lisp_error("max: not a number"); return NIL; }
        int n = UNFIXNUM(v);
        if (n > result) result = n;
        args = CDR(args);
    }
    return FIXNUM(result);
}

/* ---- Comparison ---- */

static val_t bi_num_eq(val_t args) {
    val_t a = CAR(args), b = CAR(CDR(args));
    if (!IS_FIXNUM(a) || !IS_FIXNUM(b)) { lisp_error("=: not a number"); return NIL; }
    return UNFIXNUM(a) == UNFIXNUM(b) ? sym_true : NIL;
}

static val_t bi_lt(val_t args) {
    val_t a = CAR(args), b = CAR(CDR(args));
    if (!IS_FIXNUM(a) || !IS_FIXNUM(b)) { lisp_error("<: not a number"); return NIL; }
    return UNFIXNUM(a) < UNFIXNUM(b) ? sym_true : NIL;
}

static val_t bi_gt(val_t args) {
    val_t a = CAR(args), b = CAR(CDR(args));
    if (!IS_FIXNUM(a) || !IS_FIXNUM(b)) { lisp_error(">: not a number"); return NIL; }
    return UNFIXNUM(a) > UNFIXNUM(b) ? sym_true : NIL;
}

static val_t bi_le(val_t args) {
    val_t a = CAR(args), b = CAR(CDR(args));
    if (!IS_FIXNUM(a) || !IS_FIXNUM(b)) { lisp_error("<=: not a number"); return NIL; }
    return UNFIXNUM(a) <= UNFIXNUM(b) ? sym_true : NIL;
}

static val_t bi_ge(val_t args) {
    val_t a = CAR(args), b = CAR(CDR(args));
    if (!IS_FIXNUM(a) || !IS_FIXNUM(b)) { lisp_error(">=: not a number"); return NIL; }
    return UNFIXNUM(a) >= UNFIXNUM(b) ? sym_true : NIL;
}

/* ---- Predicates ---- */

static val_t bi_null(val_t args) {
    return IS_NIL(CAR(args)) ? sym_true : NIL;
}

static val_t bi_pair(val_t args) {
    val_t v = CAR(args);
    return (IS_PTR(v) && AS_OBJ(v)->type == OBJ_CONS) ? sym_true : NIL;
}

static val_t bi_number(val_t args) {
    return IS_FIXNUM(CAR(args)) ? sym_true : NIL;
}

static val_t bi_symbol(val_t args) {
    val_t v = CAR(args);
    return (IS_PTR(v) && AS_OBJ(v)->type == OBJ_SYMBOL) ? sym_true : NIL;
}

static val_t bi_string_p(val_t args) {
    val_t v = CAR(args);
    return (IS_PTR(v) && AS_OBJ(v)->type == OBJ_STRING) ? sym_true : NIL;
}

static val_t bi_procedure(val_t args) {
    val_t v = CAR(args);
    if (!IS_PTR(v)) return NIL;
    int t = AS_OBJ(v)->type;
    return (t == OBJ_LAMBDA || t == OBJ_BUILTIN) ? sym_true : NIL;
}

static val_t bi_zero(val_t args) {
    val_t v = CAR(args);
    return (IS_FIXNUM(v) && UNFIXNUM(v) == 0) ? sym_true : NIL;
}

static val_t bi_positive(val_t args) {
    val_t v = CAR(args);
    return (IS_FIXNUM(v) && UNFIXNUM(v) > 0) ? sym_true : NIL;
}

static val_t bi_negative(val_t args) {
    val_t v = CAR(args);
    return (IS_FIXNUM(v) && UNFIXNUM(v) < 0) ? sym_true : NIL;
}

static val_t bi_not(val_t args) {
    return IS_NIL(CAR(args)) ? sym_true : NIL;
}

static val_t bi_boolean(val_t args) {
    val_t v = CAR(args);
    return (IS_NIL(v) || v == sym_true) ? sym_true : NIL;
}

/* ---- Equality ---- */

static val_t bi_eq(val_t args) {
    val_t a = CAR(args), b = CAR(CDR(args));
    return (a == b) ? sym_true : NIL;
}

static int equal_deep(val_t a, val_t b, int depth) {
    if (depth > 1000) return 0;
    if (a == b) return 1;
    if (IS_FIXNUM(a) || IS_FIXNUM(b)) return a == b;
    if (IS_NIL(a) || IS_NIL(b)) return 0;
    object_t *oa = AS_OBJ(a), *ob = AS_OBJ(b);
    if (oa->type != ob->type) return 0;
    switch (oa->type) {
    case OBJ_STRING:
        return oa->string.len == ob->string.len &&
               memcmp(oa->string.data, ob->string.data, oa->string.len) == 0;
    case OBJ_CONS:
        return equal_deep(oa->cons.car, ob->cons.car, depth + 1) &&
               equal_deep(oa->cons.cdr, ob->cons.cdr, depth + 1);
    default:
        return 0;
    }
}

static val_t bi_equal(val_t args) {
    return equal_deep(CAR(args), CAR(CDR(args)), 0) ? sym_true : NIL;
}

/* ---- List operations ---- */

static val_t bi_cons(val_t args) {
    return cons_alloc(CAR(args), CAR(CDR(args)));
}

static val_t bi_car(val_t args) {
    val_t v = CAR(args);
    if (!IS_PTR(v) || AS_OBJ(v)->type != OBJ_CONS) {
        lisp_error("car: not a pair");
        return NIL;
    }
    return CAR(v);
}

static val_t bi_cdr(val_t args) {
    val_t v = CAR(args);
    if (!IS_PTR(v) || AS_OBJ(v)->type != OBJ_CONS) {
        lisp_error("cdr: not a pair");
        return NIL;
    }
    return CDR(v);
}

static val_t bi_set_car(val_t args) {
    val_t p = CAR(args);
    if (!IS_PTR(p) || AS_OBJ(p)->type != OBJ_CONS) {
        lisp_error("set-car!: not a pair");
        return NIL;
    }
    CAR(p) = CAR(CDR(args));
    return VOID_VAL;
}

static val_t bi_set_cdr(val_t args) {
    val_t p = CAR(args);
    if (!IS_PTR(p) || AS_OBJ(p)->type != OBJ_CONS) {
        lisp_error("set-cdr!: not a pair");
        return NIL;
    }
    CDR(p) = CAR(CDR(args));
    return VOID_VAL;
}

static val_t bi_list(val_t args) {
    return args;
}

static val_t bi_length(val_t args) {
    val_t lst = CAR(args);
    int n = 0;
    while (!IS_NIL(lst)) {
        if (!IS_PTR(lst) || AS_OBJ(lst)->type != OBJ_CONS) {
            lisp_error("length: not a proper list");
            return NIL;
        }
        n++;
        lst = CDR(lst);
    }
    return FIXNUM(n);
}

static val_t bi_append(val_t args) {
    if (IS_NIL(args)) return NIL;
    if (IS_NIL(CDR(args))) return CAR(args);

    val_t first = CAR(args);
    val_t second = CAR(CDR(args));

    /* Handle more than 2 args recursively */
    if (!IS_NIL(CDR(CDR(args)))) {
        PUSH_ROOT(first);
        val_t rest_appended = bi_append(CDR(args));
        POP_ROOTS(1);
        CHECK_ERR;
        second = rest_appended;
    }

    if (IS_NIL(first)) return second;

    /* Copy first list */
    val_t head = NIL, tail = NIL;
    PUSH_ROOT(head);
    PUSH_ROOT(tail);
    PUSH_ROOT(second);
    val_t lst = first;
    while (!IS_NIL(lst)) {
        val_t cell = cons_alloc(CAR(lst), NIL);
        if (IS_NIL(head)) { head = cell; tail = cell; }
        else { CDR(tail) = cell; tail = cell; }
        lst = CDR(lst);
    }
    if (!IS_NIL(tail)) CDR(tail) = second;
    else head = second;
    POP_ROOTS(3);
    return head;
}

static val_t bi_reverse(val_t args) {
    val_t lst = CAR(args);
    val_t result = NIL;
    PUSH_ROOT(result);
    while (!IS_NIL(lst)) {
        result = cons_alloc(CAR(lst), result);
        lst = CDR(lst);
    }
    POP_ROOTS(1);
    return result;
}

static val_t bi_map(val_t args) {
    val_t fn = CAR(args);
    val_t lst = CAR(CDR(args));
    val_t head = NIL, tail = NIL;
    PUSH_ROOT(fn);
    PUSH_ROOT(lst);
    PUSH_ROOT(head);
    PUSH_ROOT(tail);

    while (!IS_NIL(lst)) {
        /* Build (fn elem) call args list */
        val_t call_args = cons_alloc(CAR(lst), NIL);
        PUSH_ROOT(call_args);
        val_t result;
        object_t *fn_obj = AS_OBJ(fn);
        if (fn_obj->type == OBJ_BUILTIN) {
            result = fn_obj->builtin.fn(call_args);
        } else if (fn_obj->type == OBJ_LAMBDA) {
            val_t new_env = env_extend(fn_obj->lambda.env, fn_obj->lambda.params, call_args);
            if (g_error) { POP_ROOTS(5); return NIL; }
            result = eval(fn_obj->lambda.body, new_env);
        } else {
            POP_ROOTS(5);
            lisp_error("map: not a procedure");
            return NIL;
        }
        POP_ROOTS(1);
        if (g_error) { POP_ROOTS(4); return NIL; }

        PUSH_ROOT(result);
        val_t cell = cons_alloc(result, NIL);
        POP_ROOTS(1);
        if (IS_NIL(head)) { head = cell; tail = cell; }
        else { CDR(tail) = cell; tail = cell; }
        lst = CDR(lst);
    }
    POP_ROOTS(4);
    return head;
}

/* ---- String operations ---- */

static val_t bi_string_length(val_t args) {
    val_t v = CAR(args);
    if (!IS_PTR(v) || AS_OBJ(v)->type != OBJ_STRING) {
        lisp_error("string-length: not a string");
        return NIL;
    }
    return FIXNUM(AS_OBJ(v)->string.len);
}

static val_t bi_string_append(val_t args) {
    /* Calculate total length */
    int total = 0;
    val_t a = args;
    while (!IS_NIL(a)) {
        val_t v = CAR(a);
        if (!IS_PTR(v) || AS_OBJ(v)->type != OBJ_STRING) {
            lisp_error("string-append: not a string");
            return NIL;
        }
        total += AS_OBJ(v)->string.len;
        a = CDR(a);
    }
    char *buf = (char *)malloc(total + 1);
    if (!buf) { lisp_error("out of memory"); return NIL; }
    int pos = 0;
    a = args;
    while (!IS_NIL(a)) {
        object_t *o = AS_OBJ(CAR(a));
        memcpy(buf + pos, o->string.data, o->string.len);
        pos += o->string.len;
        a = CDR(a);
    }
    buf[total] = 0;
    val_t result = string_alloc(buf, total);
    free(buf);
    return result;
}

static val_t bi_substring(val_t args) {
    val_t s = CAR(args);
    if (!IS_PTR(s) || AS_OBJ(s)->type != OBJ_STRING) {
        lisp_error("substring: not a string"); return NIL;
    }
    val_t start_v = CAR(CDR(args));
    val_t end_v = CAR(CDR(CDR(args)));
    if (!IS_FIXNUM(start_v) || !IS_FIXNUM(end_v)) {
        lisp_error("substring: bad index"); return NIL;
    }
    int start = UNFIXNUM(start_v);
    int end = UNFIXNUM(end_v);
    int len = AS_OBJ(s)->string.len;
    if (start < 0 || end < start || end > len) {
        lisp_error("substring: index out of range"); return NIL;
    }
    return string_alloc(AS_OBJ(s)->string.data + start, end - start);
}

static val_t bi_string_to_number(val_t args) {
    val_t v = CAR(args);
    if (!IS_PTR(v) || AS_OBJ(v)->type != OBJ_STRING) {
        lisp_error("string->number: not a string"); return NIL;
    }
    char *endptr;
    long n = strtol(AS_OBJ(v)->string.data, &endptr, 10);
    if (*endptr != 0) return NIL; /* not a valid number */
    return FIXNUM((int)n);
}

static val_t bi_number_to_string(val_t args) {
    val_t v = CAR(args);
    if (!IS_FIXNUM(v)) { lisp_error("number->string: not a number"); return NIL; }
    char buf[32];
    int n = UNFIXNUM(v);
    int neg = 0;
    unsigned int u;
    if (n < 0) { neg = 1; u = (unsigned int)(-(n + 1)) + 1u; }
    else { u = (unsigned int)n; }
    int pos = 31;
    buf[pos] = 0;
    if (u == 0) { buf[--pos] = '0'; }
    else {
        while (u > 0) { buf[--pos] = '0' + (u % 10); u /= 10; }
    }
    if (neg) buf[--pos] = '-';
    return string_alloc(buf + pos, 31 - pos);
}

/* ---- I/O ---- */

static val_t bi_display(val_t args) {
    val_t v = CAR(args);
    if (IS_PTR(v) && AS_OBJ(v)->type == OBJ_STRING) {
        /* Display strings without quotes */
        printf("%s", AS_OBJ(v)->string.data);
    } else {
        lisp_print(v);
    }
    return VOID_VAL;
}

static val_t bi_newline(val_t args) {
    (void)args;
    putchar('\n');
    return VOID_VAL;
}

static val_t bi_read(val_t args) {
    (void)args;
    int eof = 0;
    val_t v = lisp_read(&eof);
    if (eof) return symbol_intern("eof");
    return v;
}

/* ---- Control ---- */

static val_t bi_apply(val_t args) {
    val_t fn = CAR(args);
    val_t fn_args = CAR(CDR(args));
    if (!IS_PTR(fn)) { lisp_error("apply: not a procedure"); return NIL; }
    object_t *fn_obj = AS_OBJ(fn);
    if (fn_obj->type == OBJ_BUILTIN) {
        return fn_obj->builtin.fn(fn_args);
    }
    if (fn_obj->type == OBJ_LAMBDA) {
        val_t new_env = env_extend(fn_obj->lambda.env, fn_obj->lambda.params, fn_args);
        CHECK_ERR;
        return eval(fn_obj->lambda.body, new_env);
    }
    lisp_error("apply: not a procedure");
    return NIL;
}

static val_t bi_error(val_t args) {
    val_t v = CAR(args);
    if (IS_PTR(v) && AS_OBJ(v)->type == OBJ_STRING) {
        lisp_error(AS_OBJ(v)->string.data);
    } else {
        lisp_error("error");
    }
    return NIL;
}

/* ---- Registration ---- */

static void reg(val_t env, const char *name, val_t (*fn)(val_t)) {
    val_t sym = symbol_intern(name);
    PUSH_ROOT(sym);
    PUSH_ROOT(env);
    val_t b = builtin_alloc(name, fn);
    POP_ROOTS(2);
    env_define(env, sym, b);
}

void builtins_register(val_t env) {
    /* Arithmetic */
    reg(env, "+", bi_add);
    reg(env, "-", bi_sub);
    reg(env, "*", bi_mul);
    reg(env, "/", bi_div);
    reg(env, "modulo", bi_modulo);
    reg(env, "abs", bi_abs);
    reg(env, "min", bi_min);
    reg(env, "max", bi_max);

    /* Comparison */
    reg(env, "=", bi_num_eq);
    reg(env, "<", bi_lt);
    reg(env, ">", bi_gt);
    reg(env, "<=", bi_le);
    reg(env, ">=", bi_ge);

    /* Predicates */
    reg(env, "null?", bi_null);
    reg(env, "pair?", bi_pair);
    reg(env, "number?", bi_number);
    reg(env, "symbol?", bi_symbol);
    reg(env, "string?", bi_string_p);
    reg(env, "procedure?", bi_procedure);
    reg(env, "zero?", bi_zero);
    reg(env, "positive?", bi_positive);
    reg(env, "negative?", bi_negative);
    reg(env, "not", bi_not);
    reg(env, "boolean?", bi_boolean);

    /* Equality */
    reg(env, "eq?", bi_eq);
    reg(env, "equal?", bi_equal);

    /* List operations */
    reg(env, "cons", bi_cons);
    reg(env, "car", bi_car);
    reg(env, "cdr", bi_cdr);
    reg(env, "set-car!", bi_set_car);
    reg(env, "set-cdr!", bi_set_cdr);
    reg(env, "list", bi_list);
    reg(env, "length", bi_length);
    reg(env, "append", bi_append);
    reg(env, "reverse", bi_reverse);
    reg(env, "map", bi_map);

    /* String operations */
    reg(env, "string-length", bi_string_length);
    reg(env, "string-append", bi_string_append);
    reg(env, "substring", bi_substring);
    reg(env, "string->number", bi_string_to_number);
    reg(env, "number->string", bi_number_to_string);

    /* I/O */
    reg(env, "display", bi_display);
    reg(env, "newline", bi_newline);
    reg(env, "read", bi_read);

    /* Control */
    reg(env, "apply", bi_apply);
    reg(env, "error", bi_error);
}
