#include "eval.h"
#include "env.h"
#include "heap.h"
#include <string.h>

val_t global_env;

/* Interned special form symbols */
static val_t sym_quote, sym_if, sym_cond, sym_define, sym_lambda;
static val_t sym_begin, sym_let, sym_letstar, sym_and, sym_or, sym_set;
static val_t sym_else;

void eval_init(void) {
    sym_quote  = symbol_intern("quote");
    sym_if     = symbol_intern("if");
    sym_cond   = symbol_intern("cond");
    sym_define = symbol_intern("define");
    sym_lambda = symbol_intern("lambda");
    sym_begin  = symbol_intern("begin");
    sym_let    = symbol_intern("let");
    sym_letstar = symbol_intern("let*");
    sym_and    = symbol_intern("and");
    sym_or     = symbol_intern("or");
    sym_set    = symbol_intern("set!");
    sym_else   = symbol_intern("else");
}

/* Evaluate a list of expressions, return list of results */
static val_t eval_list(val_t list, val_t env) {
    if (IS_NIL(list)) return NIL;
    PUSH_ROOT(list);
    PUSH_ROOT(env);

    val_t head = NIL, tail = NIL;
    PUSH_ROOT(head);
    PUSH_ROOT(tail);

    while (!IS_NIL(list)) {
        val_t v = eval(CAR(list), env);
        CHECK_ERR; /* POP handled by caller on error path */
        PUSH_ROOT(v);
        val_t cell = cons_alloc(v, NIL);
        POP_ROOTS(1);
        if (IS_NIL(head)) {
            head = cell;
            tail = cell;
        } else {
            CDR(tail) = cell;
            tail = cell;
        }
        list = CDR(list);
    }
    POP_ROOTS(4);
    return head;
}

/* Count list length */
static int list_length(val_t list) {
    int n = 0;
    while (IS_PTR(list) && AS_OBJ(list)->type == OBJ_CONS) {
        n++;
        list = CDR(list);
    }
    return n;
}

val_t eval(val_t expr, val_t env) {
tail:
    CHECK_ERR;

    /* Self-evaluating */
    if (IS_NIL(expr) || IS_FIXNUM(expr)) return expr;

    if (!IS_PTR(expr)) return expr;

    object_t *o = AS_OBJ(expr);

    /* Symbol lookup */
    if (o->type == OBJ_SYMBOL) {
        if (expr == sym_true) return expr; /* #t is self-evaluating */
        return env_lookup(env, expr);
    }

    /* String is self-evaluating */
    if (o->type == OBJ_STRING) return expr;

    /* Must be a cons (application or special form) */
    if (o->type != OBJ_CONS) return expr;

    val_t head = CAR(expr);
    val_t rest = CDR(expr);

    /* Special forms - check by pointer equality on interned symbols */
    if (IS_PTR(head) && AS_OBJ(head)->type == OBJ_SYMBOL) {

        /* (quote datum) */
        if (head == sym_quote) {
            if (IS_NIL(rest)) { lisp_error("quote: missing argument"); return NIL; }
            return CAR(rest);
        }

        /* (if test then [else]) */
        if (head == sym_if) {
            PUSH_ROOT(rest);
            PUSH_ROOT(env);
            val_t test = eval(CAR(rest), env);
            POP_ROOTS(2);
            CHECK_ERR;
            rest = CDR(rest);
            if (!IS_NIL(test)) {
                /* then branch — tail call */
                expr = CAR(rest);
                goto tail;
            } else {
                /* else branch */
                rest = CDR(rest);
                if (IS_NIL(rest)) return NIL;
                expr = CAR(rest);
                goto tail;
            }
        }

        /* (cond (test expr...) ...) */
        if (head == sym_cond) {
            val_t clauses = rest;
            while (!IS_NIL(clauses)) {
                val_t clause = CAR(clauses);
                val_t test_expr = CAR(clause);
                val_t body = CDR(clause);

                /* (else ...) */
                if (test_expr == sym_else) {
                    /* eval body forms, tail-call last */
                    while (!IS_NIL(CDR(body))) {
                        PUSH_ROOT(body);
                        PUSH_ROOT(env);
                        eval(CAR(body), env);
                        POP_ROOTS(2);
                        CHECK_ERR;
                        body = CDR(body);
                    }
                    expr = CAR(body);
                    goto tail;
                }

                PUSH_ROOT(clauses);
                PUSH_ROOT(env);
                val_t test = eval(test_expr, env);
                POP_ROOTS(2);
                CHECK_ERR;

                if (!IS_NIL(test)) {
                    if (IS_NIL(body)) return test;
                    while (!IS_NIL(CDR(body))) {
                        PUSH_ROOT(body);
                        PUSH_ROOT(env);
                        eval(CAR(body), env);
                        POP_ROOTS(2);
                        CHECK_ERR;
                        body = CDR(body);
                    }
                    expr = CAR(body);
                    goto tail;
                }
                clauses = CDR(clauses);
            }
            return NIL;
        }

        /* (define var val) or (define (f params...) body...) */
        if (head == sym_define) {
            val_t name_or_list = CAR(rest);
            if (IS_PTR(name_or_list) && AS_OBJ(name_or_list)->type == OBJ_CONS) {
                /* (define (f params...) body...) → (define f (lambda (params...) body...)) */
                val_t fname = CAR(name_or_list);
                val_t params = CDR(name_or_list);
                val_t body_list = CDR(rest);
                PUSH_ROOT(fname);
                PUSH_ROOT(env);
                PUSH_ROOT(params);
                PUSH_ROOT(body_list);
                /* Wrap body in begin if multiple forms */
                val_t body;
                if (IS_NIL(CDR(body_list))) {
                    body = CAR(body_list);
                } else {
                    body = cons_alloc(sym_begin, body_list);
                }
                PUSH_ROOT(body);
                val_t lam = lambda_alloc(params, body, env);
                POP_ROOTS(5);
                CHECK_ERR;
                env_define(env, fname, lam);
                return VOID_VAL;
            }
            /* (define var val) */
            val_t sym = name_or_list;
            PUSH_ROOT(sym);
            PUSH_ROOT(env);
            val_t val = eval(CAR(CDR(rest)), env);
            POP_ROOTS(2);
            CHECK_ERR;
            env_define(env, sym, val);
            return VOID_VAL;
        }

        /* (lambda (params...) body...) */
        if (head == sym_lambda) {
            val_t params = CAR(rest);
            val_t body_list = CDR(rest);
            val_t body;
            if (IS_NIL(CDR(body_list))) {
                body = CAR(body_list);
            } else {
                PUSH_ROOT(params);
                PUSH_ROOT(env);
                PUSH_ROOT(body_list);
                body = cons_alloc(sym_begin, body_list);
                POP_ROOTS(3);
            }
            CHECK_ERR;
            return lambda_alloc(params, body, env);
        }

        /* (begin expr...) */
        if (head == sym_begin) {
            if (IS_NIL(rest)) return NIL;
            while (!IS_NIL(CDR(rest))) {
                PUSH_ROOT(rest);
                PUSH_ROOT(env);
                eval(CAR(rest), env);
                POP_ROOTS(2);
                CHECK_ERR;
                rest = CDR(rest);
            }
            expr = CAR(rest);
            goto tail;
        }

        /* (let ((var val) ...) body...) */
        if (head == sym_let) {
            val_t bindings = CAR(rest);
            val_t body_list = CDR(rest);
            PUSH_ROOT(bindings);
            PUSH_ROOT(env);
            PUSH_ROOT(body_list);
            val_t new_env = env_create(env);
            PUSH_ROOT(new_env);
            /* Evaluate all binding values in outer env */
            while (!IS_NIL(bindings)) {
                val_t binding = CAR(bindings);
                val_t bsym = CAR(binding);
                PUSH_ROOT(bsym);
                val_t bval = eval(CAR(CDR(binding)), env);
                POP_ROOTS(1);
                CHECK_ERR;
                env_define(new_env, bsym, bval);
                bindings = CDR(bindings);
            }
            POP_ROOTS(4);
            /* Eval body in new_env, tail-call last */
            env = new_env;
            while (!IS_NIL(CDR(body_list))) {
                PUSH_ROOT(body_list);
                PUSH_ROOT(env);
                eval(CAR(body_list), env);
                POP_ROOTS(2);
                CHECK_ERR;
                body_list = CDR(body_list);
            }
            expr = CAR(body_list);
            goto tail;
        }

        /* (let* ((var val) ...) body...) */
        if (head == sym_letstar) {
            val_t bindings = CAR(rest);
            val_t body_list = CDR(rest);
            PUSH_ROOT(bindings);
            PUSH_ROOT(env);
            PUSH_ROOT(body_list);
            val_t new_env = env_create(env);
            PUSH_ROOT(new_env);
            /* Evaluate bindings sequentially in new_env */
            while (!IS_NIL(bindings)) {
                val_t binding = CAR(bindings);
                val_t bsym = CAR(binding);
                PUSH_ROOT(bsym);
                val_t bval = eval(CAR(CDR(binding)), new_env);
                POP_ROOTS(1);
                CHECK_ERR;
                env_define(new_env, bsym, bval);
                bindings = CDR(bindings);
            }
            POP_ROOTS(4);
            env = new_env;
            while (!IS_NIL(CDR(body_list))) {
                PUSH_ROOT(body_list);
                PUSH_ROOT(env);
                eval(CAR(body_list), env);
                POP_ROOTS(2);
                CHECK_ERR;
                body_list = CDR(body_list);
            }
            expr = CAR(body_list);
            goto tail;
        }

        /* (and expr...) */
        if (head == sym_and) {
            if (IS_NIL(rest)) return sym_true;
            while (!IS_NIL(CDR(rest))) {
                PUSH_ROOT(rest);
                PUSH_ROOT(env);
                val_t v = eval(CAR(rest), env);
                POP_ROOTS(2);
                CHECK_ERR;
                if (IS_NIL(v)) return NIL;
                rest = CDR(rest);
            }
            expr = CAR(rest);
            goto tail;
        }

        /* (or expr...) */
        if (head == sym_or) {
            if (IS_NIL(rest)) return NIL;
            while (!IS_NIL(CDR(rest))) {
                PUSH_ROOT(rest);
                PUSH_ROOT(env);
                val_t v = eval(CAR(rest), env);
                POP_ROOTS(2);
                CHECK_ERR;
                if (!IS_NIL(v)) return v;
                rest = CDR(rest);
            }
            expr = CAR(rest);
            goto tail;
        }

        /* (set! var val) */
        if (head == sym_set) {
            val_t sym = CAR(rest);
            PUSH_ROOT(sym);
            PUSH_ROOT(env);
            val_t val = eval(CAR(CDR(rest)), env);
            POP_ROOTS(2);
            CHECK_ERR;
            if (!env_set(env, sym, val)) {
                lisp_error2("set!: unbound variable", AS_OBJ(sym)->symbol.name);
            }
            return VOID_VAL;
        }
    }

    /* Function application */
    PUSH_ROOT(rest);
    PUSH_ROOT(env);
    val_t fn = eval(head, env);
    POP_ROOTS(2);
    CHECK_ERR;

    if (!IS_PTR(fn)) {
        lisp_error("not a procedure");
        return NIL;
    }

    object_t *fn_obj = AS_OBJ(fn);

    if (fn_obj->type == OBJ_BUILTIN) {
        /* Evaluate arguments */
        PUSH_ROOT(fn);
        val_t args = eval_list(rest, env);
        POP_ROOTS(1);
        CHECK_ERR;
        return fn_obj->builtin.fn(args);
    }

    if (fn_obj->type == OBJ_LAMBDA) {
        PUSH_ROOT(fn);
        val_t args = eval_list(rest, env);
        POP_ROOTS(1);
        CHECK_ERR;
        val_t new_env = env_extend(fn_obj->lambda.env, fn_obj->lambda.params, args);
        CHECK_ERR;
        /* Tail call optimization */
        expr = fn_obj->lambda.body;
        env = new_env;
        goto tail;
    }

    lisp_error("not a procedure");
    return NIL;
}
