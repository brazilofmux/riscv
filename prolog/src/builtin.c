#include "term.h"
#include "engine.h"
#include "database.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Forward declaration */
void write_term(term_t t);

/* Arithmetic evaluator */
static int eval_arith(term_t expr, int *result) {
    expr = deref(expr);

    if (TAG(expr) == TAG_INT) {
        *result = UN_INT(expr);
        return 1;
    }

    if (TAG(expr) == TAG_ATOM) {
        /* Could be an atom used as 0-arity */
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "arithmetic: not evaluable: %s/0",
                 atom_name(UN_ATOM(expr)));
        return 0;
    }

    if (!IS_PTR(expr)) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "arithmetic: invalid expression");
        return 0;
    }

    int func = compound_functor(expr);
    int arity = compound_arity(expr);

    /* Unary operators */
    if (arity == 1) {
        int a;
        if (!eval_arith(compound_arg(expr, 0), &a)) return 0;

        if (func == ATOM_MINUS) { *result = -a; return 1; }
        if (func == ATOM_ABS) { *result = a < 0 ? -a : a; return 1; }

        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "arithmetic: unknown op %s/1",
                 atom_name(func));
        return 0;
    }

    /* Binary operators */
    if (arity == 2) {
        int a, b;
        if (!eval_arith(compound_arg(expr, 0), &a)) return 0;
        if (!eval_arith(compound_arg(expr, 1), &b)) return 0;

        if (func == ATOM_PLUS) { *result = a + b; return 1; }
        if (func == ATOM_MINUS) { *result = a - b; return 1; }
        if (func == ATOM_STAR) { *result = a * b; return 1; }
        if (func == ATOM_SLASH2) {
            if (b == 0) {
                g_error = 1;
                snprintf(g_errmsg, sizeof(g_errmsg), "arithmetic: division by zero");
                return 0;
            }
            *result = a / b;
            return 1;
        }
        if (func == ATOM_MOD) {
            if (b == 0) {
                g_error = 1;
                snprintf(g_errmsg, sizeof(g_errmsg), "arithmetic: division by zero");
                return 0;
            }
            *result = a % b;
            return 1;
        }
        if (func == ATOM_MIN) { *result = a < b ? a : b; return 1; }
        if (func == ATOM_MAX) { *result = a > b ? a : b; return 1; }

        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "arithmetic: unknown op %s/2",
                 atom_name(func));
        return 0;
    }

    g_error = 1;
    snprintf(g_errmsg, sizeof(g_errmsg), "arithmetic: bad expression");
    return 0;
}

static term_t build_continuation_from_stack(void) {
    if (goal_sp <= 0) return TERM_NIL;
    term_t cont = goal_stack[0];
    int i;
    for (i = 1; i < goal_sp; i++) {
        term_t args[2];
        args[0] = goal_stack[i];
        args[1] = cont;
        cont = make_compound(ATOM_COMMA, 2, args);
    }
    return cont;
}

/* Returns: 1 = succeeded, -1 = failed, 0 = not a builtin */
int try_builtin(term_t goal, int functor, int arity) {
    /* true/0 */
    if (functor == ATOM_TRUE && arity == 0) return 1;

    /* fail/0 */
    if (functor == ATOM_FAIL && arity == 0) return -1;

    /* halt/0 */
    if (functor == ATOM_HALT && arity == 0) {
        exit(0);
        return 1;
    }

    /* halt/1 */
    if (functor == ATOM_HALT && arity == 1) {
        term_t code = deref(compound_arg(goal, 0));
        int c = 0;
        if (TAG(code) == TAG_INT) c = UN_INT(code);
        exit(c);
        return 1;
    }

    /* write/1 */
    if (functor == ATOM_WRITE && arity == 1) {
        write_term(deref(compound_arg(goal, 0)));
        return 1;
    }

    /* writeln/1 */
    if (functor == ATOM_WRITELN && arity == 1) {
        write_term(deref(compound_arg(goal, 0)));
        putchar('\n');
        return 1;
    }

    /* write_canonical/1 */
    if (functor == ATOM_WRITE_CANONICAL && arity == 1) {
        write_term(deref(compound_arg(goal, 0)));
        return 1;
    }

    /* nl/0 */
    if (functor == ATOM_NL && arity == 0) {
        putchar('\n');
        return 1;
    }

    /* =/2 (unification) */
    if (functor == ATOM_UNIFY && arity == 2) {
        term_t a = compound_arg(goal, 0);
        term_t b = compound_arg(goal, 1);
        return unify(a, b) ? 1 : -1;
    }

    /* \=/2 */
    if (functor == ATOM_NOT_UNIFY && arity == 2) {
        term_t a = compound_arg(goal, 0);
        term_t b = compound_arg(goal, 1);
        /* Try unification, undo if successful */
        int saved_trail = trail_top;
        if (unify(a, b)) {
            trail_undo(saved_trail);
            return -1; /* They unify, so \= fails */
        }
        return 1; /* They don't unify, so \= succeeds */
    }

    /* ==/2 (structural equality) */
    if (functor == ATOM_EQ && arity == 2) {
        term_t a = deref(compound_arg(goal, 0));
        term_t b = deref(compound_arg(goal, 1));
        return (a == b) ? 1 : -1;
    }

    /* \==/2 */
    if (functor == ATOM_NEQ && arity == 2) {
        term_t a = deref(compound_arg(goal, 0));
        term_t b = deref(compound_arg(goal, 1));
        return (a != b) ? 1 : -1;
    }

    /* is/2 */
    if (functor == ATOM_IS && arity == 2) {
        term_t lhs = compound_arg(goal, 0);
        term_t rhs = compound_arg(goal, 1);
        int result;
        if (!eval_arith(rhs, &result)) return -1;
        return unify(lhs, MK_INT(result)) ? 1 : -1;
    }

    /* =:=/2 */
    if (functor == ATOM_ARITH_EQ && arity == 2) {
        int a, b;
        if (!eval_arith(compound_arg(goal, 0), &a)) return -1;
        if (!eval_arith(compound_arg(goal, 1), &b)) return -1;
        return (a == b) ? 1 : -1;
    }

    /* =\=/2 */
    if (functor == ATOM_ARITH_NEQ && arity == 2) {
        int a, b;
        if (!eval_arith(compound_arg(goal, 0), &a)) return -1;
        if (!eval_arith(compound_arg(goal, 1), &b)) return -1;
        return (a != b) ? 1 : -1;
    }

    /* </2 */
    if (functor == ATOM_LT && arity == 2) {
        int a, b;
        if (!eval_arith(compound_arg(goal, 0), &a)) return -1;
        if (!eval_arith(compound_arg(goal, 1), &b)) return -1;
        return (a < b) ? 1 : -1;
    }

    /* >/2 */
    if (functor == ATOM_GT && arity == 2) {
        int a, b;
        if (!eval_arith(compound_arg(goal, 0), &a)) return -1;
        if (!eval_arith(compound_arg(goal, 1), &b)) return -1;
        return (a > b) ? 1 : -1;
    }

    /* =</2 */
    if (functor == ATOM_LE && arity == 2) {
        int a, b;
        if (!eval_arith(compound_arg(goal, 0), &a)) return -1;
        if (!eval_arith(compound_arg(goal, 1), &b)) return -1;
        return (a <= b) ? 1 : -1;
    }

    /* >=/2 */
    if (functor == ATOM_GE && arity == 2) {
        int a, b;
        if (!eval_arith(compound_arg(goal, 0), &a)) return -1;
        if (!eval_arith(compound_arg(goal, 1), &b)) return -1;
        return (a >= b) ? 1 : -1;
    }

    /* atom/1 */
    if (functor == ATOM_ATOM && arity == 1) {
        term_t a = deref(compound_arg(goal, 0));
        return (TAG(a) == TAG_ATOM) ? 1 : -1;
    }

    /* integer/1 */
    if (functor == ATOM_INTEGER && arity == 1) {
        term_t a = deref(compound_arg(goal, 0));
        return (TAG(a) == TAG_INT) ? 1 : -1;
    }

    /* number/1 */
    if (functor == ATOM_NUMBER && arity == 1) {
        term_t a = deref(compound_arg(goal, 0));
        return (TAG(a) == TAG_INT) ? 1 : -1;
    }

    /* var/1 */
    if (functor == ATOM_VAR && arity == 1) {
        term_t a = deref(compound_arg(goal, 0));
        return (TAG(a) == TAG_VAR) ? 1 : -1;
    }

    /* nonvar/1 */
    if (functor == ATOM_NONVAR && arity == 1) {
        term_t a = deref(compound_arg(goal, 0));
        return (TAG(a) != TAG_VAR) ? 1 : -1;
    }

    /* compound/1 */
    if (functor == ATOM_COMPOUND && arity == 1) {
        term_t a = deref(compound_arg(goal, 0));
        return IS_PTR(a) ? 1 : -1;
    }

    /* is_list/1 */
    if (functor == ATOM_IS_LIST && arity == 1) {
        term_t a = deref(compound_arg(goal, 0));
        while (IS_PTR(a) && compound_functor(a) == ATOM_DOT && compound_arity(a) == 2) {
            a = deref(compound_arg(a, 1));
        }
        return (TAG(a) == TAG_ATOM && UN_ATOM(a) == ATOM_NIL_LIST) ? 1 : -1;
    }

    /* functor/3 */
    if (functor == ATOM_FUNCTOR && arity == 3) {
        term_t t = deref(compound_arg(goal, 0));
        term_t f = compound_arg(goal, 1);
        term_t a = compound_arg(goal, 2);

        if (TAG(t) != TAG_VAR) {
            /* Decompose */
            int tf = term_functor(t);
            int ta = term_arity(t);
            if (tf < 0) {
                /* integer */
                if (!unify(f, t)) return -1;
                return unify(a, MK_INT(0)) ? 1 : -1;
            }
            if (!unify(f, MK_ATOM(tf))) return -1;
            return unify(a, MK_INT(ta)) ? 1 : -1;
        } else {
            /* Construct */
            f = deref(f);
            a = deref(a);
            if (TAG(a) != TAG_INT) return -1;
            int ar = UN_INT(a);
            if (ar < 0 || ar > PROLOG_MAX_ARITY) {
                g_error = 1;
                snprintf(g_errmsg, sizeof(g_errmsg), "functor/3: arity out of range");
                return -1;
            }
            if (ar == 0) {
                return unify(t, f) ? 1 : -1;
            }
            if (TAG(f) != TAG_ATOM) return -1;
            term_t args[PROLOG_MAX_ARITY];
            int i;
            for (i = 0; i < ar; i++)
                args[i] = fresh_var();
            term_t compound = make_compound(UN_ATOM(f), ar, args);
            return unify(t, compound) ? 1 : -1;
        }
    }

    /* arg/3 */
    if (functor == ATOM_ARG && arity == 3) {
        term_t n = deref(compound_arg(goal, 0));
        term_t t = deref(compound_arg(goal, 1));
        term_t a = compound_arg(goal, 2);
        if (TAG(n) != TAG_INT || !IS_PTR(t)) return -1;
        int idx = UN_INT(n);
        int ar = compound_arity(t);
        if (idx < 1 || idx > ar) return -1;
        return unify(a, compound_arg(t, idx - 1)) ? 1 : -1;
    }

    /* =../2 (univ) */
    if (functor == ATOM_UNIV && arity == 2) {
        term_t t = deref(compound_arg(goal, 0));
        term_t l = compound_arg(goal, 1);

        if (TAG(t) != TAG_VAR) {
            /* Decompose term to list */
            term_t list;
            if (IS_PTR(t)) {
                int f = compound_functor(t);
                int a = compound_arity(t);
                list = MK_ATOM(ATOM_NIL_LIST);
                int i;
                for (i = a - 1; i >= 0; i--)
                    list = make_list_cons(compound_arg(t, i), list);
                list = make_list_cons(MK_ATOM(f), list);
            } else {
                list = make_list_cons(t, MK_ATOM(ATOM_NIL_LIST));
            }
            return unify(l, list) ? 1 : -1;
        } else {
            /* Construct term from list */
            l = deref(l);
            if (!IS_PTR(l) || compound_functor(l) != ATOM_DOT) return -1;
            term_t head = deref(compound_arg(l, 0));
            term_t rest = deref(compound_arg(l, 1));
            if (TAG(head) == TAG_ATOM && UN_ATOM(head) == ATOM_NIL_LIST) return -1;

            /* Count args */
            int count = 0;
            term_t r = rest;
            while (IS_PTR(r) && compound_functor(r) == ATOM_DOT && compound_arity(r) == 2) {
                count++;
                r = deref(compound_arg(r, 1));
            }

            if (count == 0) {
                return unify(t, head) ? 1 : -1;
            }

            if (TAG(head) != TAG_ATOM) return -1;
            if (count < 0 || count > PROLOG_MAX_ARITY) {
                g_error = 1;
                snprintf(g_errmsg, sizeof(g_errmsg), "=../2: arity out of range");
                return -1;
            }
            term_t args[PROLOG_MAX_ARITY];
            r = rest;
            int i;
            for (i = 0; i < count; i++) {
                args[i] = compound_arg(r, 0);
                r = deref(compound_arg(r, 1));
            }
            term_t compound = make_compound(UN_ATOM(head), count, args);
            return unify(t, compound) ? 1 : -1;
        }
    }

    /* copy_term/2 */
    if (functor == ATOM_COPY_TERM && arity == 2) {
        term_t orig = compound_arg(goal, 0);
        term_t copy_out = compound_arg(goal, 1);
        int vm[256];
        memset(vm, -1, sizeof(vm));
        term_t copied = copy_term(orig, vm, 256);
        return unify(copy_out, copied) ? 1 : -1;
    }

    /* assert/1, assertz/1 */
    if ((functor == ATOM_ASSERT || functor == ATOM_ASSERTZ) && arity == 1) {
        term_t clause = deref(compound_arg(goal, 0));
        term_t head, body;
        if (IS_PTR(clause) && compound_functor(clause) == ATOM_CLAUSE && compound_arity(clause) == 2) {
            head = deref(compound_arg(clause, 0));
            body = deref(compound_arg(clause, 1));
        } else {
            head = clause;
            body = MK_ATOM(ATOM_TRUE);
        }
        /* Copy to code heap */
        int vm[256];
        memset(vm, -1, sizeof(vm));
        /* Count vars in the term */
        term_t ch = copy_term_code(head, vm, 256);
        term_t cb = copy_term_code(body, vm, 256);
        int nv = 0;
        int i;
        for (i = 0; i < 256; i++)
            if (vm[i] != -1 && vm[i] >= nv) nv = vm[i] + 1;
        db_add_clause(ch, cb, nv, 0);
        return 1;
    }

    /* asserta/1 */
    if (functor == ATOM_ASSERTA && arity == 1) {
        term_t clause = deref(compound_arg(goal, 0));
        term_t head, body;
        if (IS_PTR(clause) && compound_functor(clause) == ATOM_CLAUSE && compound_arity(clause) == 2) {
            head = deref(compound_arg(clause, 0));
            body = deref(compound_arg(clause, 1));
        } else {
            head = clause;
            body = MK_ATOM(ATOM_TRUE);
        }
        int vm[256];
        memset(vm, -1, sizeof(vm));
        term_t ch = copy_term_code(head, vm, 256);
        term_t cb = copy_term_code(body, vm, 256);
        int nv = 0;
        int i;
        for (i = 0; i < 256; i++)
            if (vm[i] != -1 && vm[i] >= nv) nv = vm[i] + 1;
        db_add_clause(ch, cb, nv, 1);
        return 1;
    }

    /* retract/1 */
    if (functor == ATOM_RETRACT && arity == 1) {
        term_t clause = deref(compound_arg(goal, 0));
        term_t head, body;
        if (IS_PTR(clause) && compound_functor(clause) == ATOM_CLAUSE && compound_arity(clause) == 2) {
            head = deref(compound_arg(clause, 0));
            body = deref(compound_arg(clause, 1));
        } else {
            head = clause;
            body = TERM_NIL;
        }
        return db_retract(head, body) ? 1 : -1;
    }

    /* findall/3 */
    if (functor == ATOM_FINDALL && arity == 3) {
        term_t tmpl = compound_arg(goal, 0);
        term_t inner_goal = compound_arg(goal, 1);
        term_t result_list = compound_arg(goal, 2);

        /* Save state and run an isolated search loop. */
        int saved_trail = trail_top;
        int saved_hp = hp;
        int saved_vc = var_count;
        int saved_gsp = goal_sp;
        int saved_cp = choice_top;
        int saved_cb = cut_barrier;

        term_t persisted[256];
        int rcount = 0;

        goal_sp = 0;
        choice_top = 0;
        cut_barrier = 0;
        goal_stack[goal_sp++] = inner_goal;

        for (;;) {
            while (goal_sp > 0) {
                term_t fg = deref(goal_stack[--goal_sp]);

                if (fg == TERM_NIL || (TAG(fg) == TAG_ATOM && UN_ATOM(fg) == ATOM_TRUE))
                    continue;
                if (TAG(fg) == TAG_ATOM && UN_ATOM(fg) == ATOM_FAIL)
                    goto findall_backtrack;

                if (IS_PTR(fg) && compound_functor(fg) == ATOM_COMMA && compound_arity(fg) == 2) {
                    if (goal_sp + 2 > GOAL_STACK_SIZE) {
                        g_error = 1;
                        snprintf(g_errmsg, sizeof(g_errmsg), "goal stack overflow");
                        goto findall_done;
                    }
                    goal_stack[goal_sp++] = compound_arg(fg, 1);
                    goal_stack[goal_sp++] = compound_arg(fg, 0);
                    continue;
                }

                {
                    int ff = term_functor(fg);
                    int fa = term_arity(fg);
                    int bres = try_builtin(fg, ff, fa);
                    if (bres == 1) continue;
                    if (bres == -1) goto findall_backtrack;
                }

                {
                    int ff = term_functor(fg);
                    int fa = term_arity(fg);
                    predicate_t *pp = db_lookup(ff, fa);
                    if (!pp || !pp->first) goto findall_backtrack;

                    clause_t *cc = pp->first;
                    if (cc->next) {
                        if (choice_top >= MAX_CHOICES) {
                            g_error = 1;
                            snprintf(g_errmsg, sizeof(g_errmsg), "choice stack overflow");
                            goto findall_done;
                        }
                        choice_t *cp = &choices[choice_top++];
                        cp->trail_top = trail_top;
                        cp->var_count = var_count;
                        cp->next_clause = cc->next;
                        cp->goal = fg;
                        cp->cut_barrier = cut_barrier;
                        cp->continuation = build_continuation_from_stack();
                        cp->hp = hp;
                    }

                    int vm[256];
                    memset(vm, -1, sizeof(vm));
                    term_t ch = copy_term_code(cc->head, vm, 256);
                    term_t cb = cc->body != TERM_NIL ? copy_term_code(cc->body, vm, 256) : TERM_NIL;
                    if (g_error || !unify(fg, ch)) goto findall_backtrack;
                    if (cb != TERM_NIL && !(TAG(cb) == TAG_ATOM && UN_ATOM(cb) == ATOM_TRUE)) {
                        if (goal_sp >= GOAL_STACK_SIZE) {
                            g_error = 1;
                            snprintf(g_errmsg, sizeof(g_errmsg), "goal stack overflow");
                            goto findall_done;
                        }
                        goal_stack[goal_sp++] = cb;
                    }
                }
            }

            if (rcount < 256) {
                int cvm[256];
                memset(cvm, -1, sizeof(cvm));
                persisted[rcount++] = persist_term(copy_term(tmpl, cvm, 256));
                if (g_error) goto findall_done;
            }
findall_backtrack:
            if (choice_top <= 0) break;

            {
                choice_t *cp = &choices[--choice_top];
                trail_undo(cp->trail_top);
                hp = cp->hp;
                var_count = cp->var_count;
                cut_barrier = cp->cut_barrier;
                goal_sp = 0;
                if (cp->continuation != TERM_NIL) {
                    goal_stack[goal_sp++] = cp->continuation;
                }

                clause_t *next = (clause_t *)cp->next_clause;
                term_t retry_goal = cp->goal;

                if (next == 0) {
                    goal_stack[goal_sp++] = retry_goal;
                    continue;
                }

                if (next->next) {
                    if (choice_top >= MAX_CHOICES) {
                        g_error = 1;
                        snprintf(g_errmsg, sizeof(g_errmsg), "choice stack overflow");
                        goto findall_done;
                    }
                    choice_t *ncp = &choices[choice_top++];
                    ncp->trail_top = trail_top;
                    ncp->var_count = var_count;
                    ncp->next_clause = next->next;
                    ncp->goal = retry_goal;
                    ncp->cut_barrier = cp->cut_barrier;
                    ncp->continuation = build_continuation_from_stack();
                    ncp->hp = hp;
                }

                {
                    int vm2[256];
                    memset(vm2, -1, sizeof(vm2));
                    term_t ch2 = copy_term_code(next->head, vm2, 256);
                    term_t cb2 = next->body != TERM_NIL ? copy_term_code(next->body, vm2, 256) : TERM_NIL;
                    if (g_error || !unify(retry_goal, ch2)) continue;
                    if (cb2 != TERM_NIL && !(TAG(cb2) == TAG_ATOM && UN_ATOM(cb2) == ATOM_TRUE)) {
                        if (goal_sp >= GOAL_STACK_SIZE) {
                            g_error = 1;
                            snprintf(g_errmsg, sizeof(g_errmsg), "goal stack overflow");
                            goto findall_done;
                        }
                        goal_stack[goal_sp++] = cb2;
                    }
                }
            }
        }

findall_done:
        trail_undo(saved_trail);
        hp = saved_hp;
        var_count = saved_vc;
        goal_sp = saved_gsp;
        choice_top = saved_cp;
        cut_barrier = saved_cb;

        if (g_error) return -1;

        term_t rlist = MK_ATOM(ATOM_NIL_LIST);
        int i;
        for (i = rcount - 1; i >= 0; i--)
            rlist = make_list_cons(persisted[i], rlist);

        return unify(result_list, rlist) ? 1 : -1;
    }

    /* atom_length/2 */
    if (functor == ATOM_ATOM_LENGTH && arity == 2) {
        term_t a = deref(compound_arg(goal, 0));
        term_t l = compound_arg(goal, 1);
        if (TAG(a) != TAG_ATOM) return -1;
        int len = strlen(atom_name(UN_ATOM(a)));
        return unify(l, MK_INT(len)) ? 1 : -1;
    }

    /* atom_chars/2 */
    if (functor == ATOM_ATOM_CHARS && arity == 2) {
        term_t a = deref(compound_arg(goal, 0));
        term_t l = compound_arg(goal, 1);
        if (TAG(a) == TAG_ATOM) {
            /* Decompose */
            const char *s = atom_name(UN_ATOM(a));
            term_t list = MK_ATOM(ATOM_NIL_LIST);
            int i;
            int slen = strlen(s);
            for (i = slen - 1; i >= 0; i--) {
                char ch[2];
                ch[0] = s[i]; ch[1] = 0;
                list = make_list_cons(MK_ATOM(atom_intern(ch)), list);
            }
            return unify(l, list) ? 1 : -1;
        }
        /* Compose from list */
        l = deref(l);
        char buf[256];
        int pos = 0;
        while (IS_PTR(l) && compound_functor(l) == ATOM_DOT && compound_arity(l) == 2 && pos < 255) {
            term_t ch = deref(compound_arg(l, 0));
            if (TAG(ch) == TAG_ATOM) {
                const char *cn = atom_name(UN_ATOM(ch));
                buf[pos++] = cn[0];
            }
            l = deref(compound_arg(l, 1));
        }
        buf[pos] = 0;
        return unify(a, MK_ATOM(atom_intern(buf))) ? 1 : -1;
    }

    /* char_code/2 */
    if (functor == ATOM_CHAR_CODE && arity == 2) {
        term_t ch = deref(compound_arg(goal, 0));
        term_t code = compound_arg(goal, 1);
        if (TAG(ch) == TAG_ATOM) {
            const char *s = atom_name(UN_ATOM(ch));
            return unify(code, MK_INT(s[0])) ? 1 : -1;
        }
        if (TAG(deref(code)) == TAG_INT) {
            char buf[2];
            buf[0] = (char)UN_INT(deref(code));
            buf[1] = 0;
            return unify(ch, MK_ATOM(atom_intern(buf))) ? 1 : -1;
        }
        return -1;
    }

    /* number_chars/2 */
    if (functor == ATOM_NUMBER_CHARS && arity == 2) {
        term_t n = deref(compound_arg(goal, 0));
        term_t l = compound_arg(goal, 1);
        if (TAG(n) == TAG_INT) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", (int)UN_INT(n));
            term_t list = MK_ATOM(ATOM_NIL_LIST);
            int i;
            int slen = strlen(buf);
            for (i = slen - 1; i >= 0; i--) {
                char ch[2];
                ch[0] = buf[i]; ch[1] = 0;
                list = make_list_cons(MK_ATOM(atom_intern(ch)), list);
            }
            return unify(l, list) ? 1 : -1;
        }
        return -1;
    }

    /* atom_concat/3 */
    if (functor == ATOM_ATOM_CONCAT && arity == 3) {
        term_t a = deref(compound_arg(goal, 0));
        term_t b = deref(compound_arg(goal, 1));
        term_t c = compound_arg(goal, 2);
        if (TAG(a) != TAG_ATOM || TAG(b) != TAG_ATOM) return -1;
        char buf[512];
        snprintf(buf, sizeof(buf), "%s%s", atom_name(UN_ATOM(a)), atom_name(UN_ATOM(b)));
        return unify(c, MK_ATOM(atom_intern(buf))) ? 1 : -1;
    }

    /* succ/2 */
    if (functor == ATOM_SUCC && arity == 2) {
        term_t a = deref(compound_arg(goal, 0));
        term_t b = compound_arg(goal, 1);
        if (TAG(a) == TAG_INT) {
            int v = UN_INT(a);
            if (v < 0) return -1;
            return unify(b, MK_INT(v + 1)) ? 1 : -1;
        }
        b = deref(b);
        if (TAG(b) == TAG_INT) {
            int v = UN_INT(b);
            if (v <= 0) return -1;
            return unify(a, MK_INT(v - 1)) ? 1 : -1;
        }
        return -1;
    }

    /* plus/3 */
    if (functor == ATOM_PLUS2 && arity == 3) {
        term_t a = deref(compound_arg(goal, 0));
        term_t b = deref(compound_arg(goal, 1));
        term_t c = compound_arg(goal, 2);
        if (TAG(a) == TAG_INT && TAG(b) == TAG_INT) {
            return unify(c, MK_INT(UN_INT(a) + UN_INT(b))) ? 1 : -1;
        }
        return -1;
    }

    /* Not a builtin */
    return 0;
}
