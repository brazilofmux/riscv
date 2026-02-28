#include "term.h"
#include "engine.h"

/* Iterative unification with explicit stack */
#define UNIFY_STACK_SIZE 4096

static term_t ustack[UNIFY_STACK_SIZE];
static int usp;

int unify(term_t a, term_t b) {
    usp = 0;
    ustack[usp++] = a;
    ustack[usp++] = b;

    while (usp > 0) {
        b = deref(ustack[--usp]);
        a = deref(ustack[--usp]);

        if (a == b) continue;

        if (TAG(a) == TAG_VAR) {
            bind(UN_VAR(a), b);
            continue;
        }
        if (TAG(b) == TAG_VAR) {
            bind(UN_VAR(b), a);
            continue;
        }

        /* Both must be compound with same functor/arity */
        if (!IS_PTR(a) || !IS_PTR(b)) return 0;

        int fa = compound_functor(a);
        int fb = compound_functor(b);
        if (fa != fb) return 0;

        int aa = compound_arity(a);
        int ab = compound_arity(b);
        if (aa != ab) return 0;

        if (usp + aa * 2 > UNIFY_STACK_SIZE) return 0;

        int i;
        for (i = 0; i < aa; i++) {
            ustack[usp++] = compound_arg(a, i);
            ustack[usp++] = compound_arg(b, i);
        }
    }
    return 1;
}
