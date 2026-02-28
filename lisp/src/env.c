#include "env.h"
#include "heap.h"

/* Environment: (bindings . parent)
 * bindings: ((sym . val) (sym . val) ...)
 */

val_t env_create(val_t parent) {
    return cons_alloc(NIL, parent);
}

val_t env_lookup(val_t env, val_t sym) {
    val_t e = env;
    while (!IS_NIL(e)) {
        val_t bindings = CAR(e);
        while (!IS_NIL(bindings)) {
            val_t pair = CAR(bindings);
            if (CAR(pair) == sym) {
                return CDR(pair);
            }
            bindings = CDR(bindings);
        }
        e = CDR(e);
    }
    if (IS_PTR(sym) && AS_OBJ(sym)->type == OBJ_SYMBOL)
        lisp_error2("unbound variable", AS_OBJ(sym)->symbol.name);
    else
        lisp_error("unbound variable");
    return NIL;
}

void env_define(val_t env, val_t sym, val_t val) {
    PUSH_ROOT(env);
    PUSH_ROOT(sym);
    PUSH_ROOT(val);
    val_t pair = cons_alloc(sym, val);
    PUSH_ROOT(pair);
    val_t new_bindings = cons_alloc(pair, CAR(env));
    POP_ROOTS(4);
    CAR(env) = new_bindings;
}

int env_set(val_t env, val_t sym, val_t val) {
    val_t e = env;
    while (!IS_NIL(e)) {
        val_t bindings = CAR(e);
        while (!IS_NIL(bindings)) {
            val_t pair = CAR(bindings);
            if (CAR(pair) == sym) {
                CDR(pair) = val;
                return 1;
            }
            bindings = CDR(bindings);
        }
        e = CDR(e);
    }
    return 0;
}

val_t env_extend(val_t parent, val_t params, val_t args) {
    PUSH_ROOT(parent);
    PUSH_ROOT(params);
    PUSH_ROOT(args);
    val_t new_env = env_create(parent);
    PUSH_ROOT(new_env);
    val_t p = params;
    val_t a = args;
    while (!IS_NIL(p) && IS_PTR(p)) {
        if (AS_OBJ(p)->type == OBJ_SYMBOL) {
            /* rest parameter: bind remaining args */
            env_define(new_env, p, a);
            POP_ROOTS(4);
            return new_env;
        }
        if (IS_NIL(a)) {
            lisp_error("too few arguments");
            POP_ROOTS(4);
            return NIL;
        }
        env_define(new_env, CAR(p), CAR(a));
        p = CDR(p);
        a = CDR(a);
    }
    POP_ROOTS(4);
    return new_env;
}
