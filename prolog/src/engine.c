#include "engine.h"
#include "term.h"
#include "database.h"
#include <string.h>
#include <stdio.h>

/* Variable bindings */
term_t var_binding[MAX_VARS];
int var_count = 0;

/* Trail */
int trail[TRAIL_SIZE];
int trail_top = 0;

/* Goal stack */
term_t goal_stack[GOAL_STACK_SIZE];
int goal_sp = 0;

/* Choice points */
choice_t choices[MAX_CHOICES];
int choice_top = 0;

/* Cut barrier */
int cut_barrier = 0;

term_t deref(term_t t) {
    while (TAG(t) == TAG_VAR) {
        int id = UN_VAR(t);
        if (id >= MAX_VARS || id >= var_count) return t;
        term_t val = var_binding[id];
        if (val == TERM_NIL) return t;
        t = val;
    }
    return t;
}

void bind(int var_id, term_t val) {
    if (var_id < 0 || var_id >= MAX_VARS || var_id >= var_count) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "invalid variable id");
        return;
    }
    if (trail_top >= TRAIL_SIZE) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "trail overflow");
        return;
    }
    trail[trail_top++] = var_id;
    var_binding[var_id] = val;
}

void trail_undo(int saved_top) {
    while (trail_top > saved_top) {
        trail_top--;
        var_binding[trail[trail_top]] = TERM_NIL;
    }
}

term_t fresh_var(void) {
    if (var_count >= MAX_VARS) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "too many variables");
        return TERM_NIL;
    }
    int id = var_count++;
    var_binding[id] = TERM_NIL;
    return MK_VAR(id);
}

static term_t copy_term_impl(term_t t, int *var_map, int map_size, int follow_bindings) {
    if (follow_bindings) t = deref(t);

    switch (TAG(t)) {
    case TAG_INT:
    case TAG_ATOM:
        return t;

    case TAG_VAR: {
        int id = UN_VAR(t);
        if (id >= map_size) return t;
        if (var_map[id] == -1) {
            if (var_count >= MAX_VARS) {
                g_error = 1;
                return TERM_NIL;
            }
            var_map[id] = var_count++;
            var_binding[var_map[id]] = TERM_NIL;
        }
        return MK_VAR(var_map[id]);
    }

    default: /* PTR */
        if (!IS_PTR(t)) return t;
        {
            int func = compound_functor(t);
            int arity = compound_arity(t);
            if (arity < 0 || arity > PROLOG_MAX_ARITY) {
                g_error = 1;
                snprintf(g_errmsg, sizeof(g_errmsg), "arity overflow");
                return TERM_NIL;
            }
            term_t args[PROLOG_MAX_ARITY];
            int i;
            for (i = 0; i < arity; i++) {
                args[i] = copy_term_impl(compound_arg(t, i), var_map, map_size, follow_bindings);
                if (g_error) return TERM_NIL;
            }
            return make_compound(func, arity, args);
        }
    }
}

term_t copy_term(term_t t, int *var_map, int map_size) {
    return copy_term_impl(t, var_map, map_size, 1);
}

term_t copy_term_code(term_t t, int *var_map, int map_size) {
    return copy_term_impl(t, var_map, map_size, 0);
}

void engine_reset(void) {
    trail_top = 0;
    goal_sp = 0;
    choice_top = 0;
    cut_barrier = 0;
    hp = 1;
    int i;
    for (i = 0; i < var_count && i < MAX_VARS; i++)
        var_binding[i] = TERM_NIL;
    var_count = 0;
}

/* Build conjunction from remaining goal stack (top..bottom order). */
static term_t build_continuation(void) {
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

/* External builtin dispatcher (defined in builtin.c) */
extern int try_builtin(term_t goal, int functor, int arity);

/* Iterative solve loop */
int solve(term_t goal) {
    goal_sp = 0;
    choice_top = 0;
    cut_barrier = 0;

    if (goal_sp >= GOAL_STACK_SIZE) return 0;
    goal_stack[goal_sp++] = goal;

    for (;;) {
        if (g_error) return 0;

        if (goal_sp <= 0) return 1;

        term_t g = deref(goal_stack[--goal_sp]);

        if (g == TERM_NIL || (TAG(g) == TAG_ATOM && UN_ATOM(g) == ATOM_TRUE))
            continue;

        if (TAG(g) == TAG_ATOM && UN_ATOM(g) == ATOM_FAIL)
            goto backtrack;

        /* Conjunction: (A, B) */
        if (IS_PTR(g) && compound_functor(g) == ATOM_COMMA && compound_arity(g) == 2) {
            term_t left = compound_arg(g, 0);
            term_t right = compound_arg(g, 1);
            if (goal_sp + 2 > GOAL_STACK_SIZE) {
                g_error = 1;
                snprintf(g_errmsg, sizeof(g_errmsg), "goal stack overflow");
                return 0;
            }
            goal_stack[goal_sp++] = right;
            goal_stack[goal_sp++] = left;
            continue;
        }

        /* Disjunction: (A ; B) */
        if (IS_PTR(g) && compound_functor(g) == ATOM_SEMI && compound_arity(g) == 2) {
            term_t left = compound_arg(g, 0);
            term_t right = compound_arg(g, 1);

            /* If-then-else: (Cond -> Then ; Else) */
            term_t dl = deref(left);
            if (IS_PTR(dl) && compound_functor(dl) == ATOM_ARROW && compound_arity(dl) == 2) {
                term_t cond = compound_arg(dl, 0);
                term_t then_goal = compound_arg(dl, 1);

                if (choice_top >= MAX_CHOICES) { g_error = 1; return 0; }
                choice_t *cp = &choices[choice_top++];
                cp->trail_top = trail_top;
                cp->var_count = var_count;
                cp->next_clause = 0;
                cp->goal = right;
                cp->cut_barrier = cut_barrier;
                cp->continuation = build_continuation();
                cp->hp = hp;

                goal_stack[goal_sp++] = then_goal;
                goal_stack[goal_sp++] = MK_ATOM(ATOM_CUT);
                goal_stack[goal_sp++] = cond;
                continue;
            }

            /* Plain disjunction */
            if (choice_top >= MAX_CHOICES) { g_error = 1; return 0; }
            choice_t *cp = &choices[choice_top++];
            cp->trail_top = trail_top;
            cp->var_count = var_count;
            cp->next_clause = 0;
            cp->goal = right;
            cp->cut_barrier = cut_barrier;
            cp->continuation = build_continuation();
            cp->hp = hp;

            goal_stack[goal_sp++] = left;
            continue;
        }

        /* If-then without else */
        if (IS_PTR(g) && compound_functor(g) == ATOM_ARROW && compound_arity(g) == 2) {
            goal_stack[goal_sp++] = compound_arg(g, 1);
            goal_stack[goal_sp++] = compound_arg(g, 0);
            continue;
        }

        /* Negation as failure: \+(Goal) */
        if (IS_PTR(g) && compound_functor(g) == ATOM_NOT && compound_arity(g) == 1) {
            term_t inner = compound_arg(g, 0);
            int saved_trail = trail_top;
            int saved_hp = hp;
            int saved_vc = var_count;
            int saved_gsp = goal_sp;
            int saved_cp = choice_top;
            int saved_cb = cut_barrier;
            term_t saved_goals[64];
            choice_t saved_choices[64];
            int sg_count = goal_sp < 64 ? goal_sp : 64;
            int sc_count = choice_top < 64 ? choice_top : 64;
            int j;
            for (j = 0; j < sg_count; j++)
                saved_goals[j] = goal_stack[j];
            for (j = 0; j < sc_count; j++)
                saved_choices[j] = choices[j];

            int inner_result = solve(inner);

            trail_undo(saved_trail);
            hp = saved_hp;
            var_count = saved_vc;
            goal_sp = saved_gsp;
            choice_top = saved_cp;
            cut_barrier = saved_cb;
            for (j = 0; j < sg_count; j++)
                goal_stack[j] = saved_goals[j];
            for (j = 0; j < sc_count; j++)
                choices[j] = saved_choices[j];

            if (inner_result)
                goto backtrack;
            continue;
        }

        /* Cut */
        if (TAG(g) == TAG_ATOM && UN_ATOM(g) == ATOM_CUT) {
            if (choice_top > cut_barrier)
                choice_top = cut_barrier;
            continue;
        }

        int functor = term_functor(g);
        int arity = term_arity(g);

        if (functor < 0) goto backtrack;

        /* Try builtin */
        {
            int bres = try_builtin(g, functor, arity);
            if (bres == 1) continue;
            if (bres == -1) goto backtrack;
        }

        /* Database lookup */
        {
            predicate_t *pred = db_lookup(functor, arity);
            if (!pred || !pred->first) goto backtrack;

            clause_t *clause = pred->first;

            if (clause->next) {
                if (choice_top >= MAX_CHOICES) { g_error = 1; return 0; }
                choice_t *cp = &choices[choice_top++];
                cp->trail_top = trail_top;
                cp->var_count = var_count;
                cp->next_clause = clause->next;
                cp->goal = g;
                cp->cut_barrier = cut_barrier;
                cp->continuation = build_continuation();
                cp->hp = hp;
            }

            int var_map[256];
            memset(var_map, -1, sizeof(var_map));
            term_t ch = copy_term_code(clause->head, var_map, 256);
            term_t cb = clause->body != TERM_NIL ?
                        copy_term_code(clause->body, var_map, 256) : TERM_NIL;

            if (g_error) return 0;

            if (!unify(g, ch)) {
                goto backtrack;
            }

            if (cb != TERM_NIL && !(TAG(cb) == TAG_ATOM && UN_ATOM(cb) == ATOM_TRUE))
                goal_stack[goal_sp++] = cb;
        }
        continue;

backtrack:
        if (choice_top <= 0) return 0;

        {
            choice_t *cp = &choices[--choice_top];
            trail_undo(cp->trail_top);
            hp = cp->hp;
            var_count = cp->var_count;
            cut_barrier = cp->cut_barrier;

            goal_sp = 0;
            if (cp->continuation != TERM_NIL) {
                if (goal_sp >= GOAL_STACK_SIZE) {
                    g_error = 1;
                    snprintf(g_errmsg, sizeof(g_errmsg), "goal stack overflow");
                    return 0;
                }
                goal_stack[goal_sp++] = cp->continuation;
            }

            clause_t *next = (clause_t *)cp->next_clause;
            term_t retry_goal = cp->goal;

            if (next == 0) {
                /* Disjunction: push the right branch */
                goal_stack[goal_sp++] = retry_goal;
                continue;
            }

            /* Try next clause */
            if (next->next) {
                if (choice_top >= MAX_CHOICES) { g_error = 1; return 0; }
                choice_t *ncp = &choices[choice_top++];
                ncp->trail_top = trail_top;
                ncp->var_count = var_count;
                ncp->next_clause = next->next;
                ncp->goal = retry_goal;
                ncp->cut_barrier = cp->cut_barrier;
                ncp->continuation = build_continuation();
                ncp->hp = hp;
            }

            int vm2[256];
            memset(vm2, -1, sizeof(vm2));
            term_t ch2 = copy_term_code(next->head, vm2, 256);
            term_t cb2 = next->body != TERM_NIL ?
                         copy_term_code(next->body, vm2, 256) : TERM_NIL;

            if (g_error) return 0;

            if (!unify(retry_goal, ch2)) {
                goto backtrack;
            }

            if (cb2 != TERM_NIL && !(TAG(cb2) == TAG_ATOM && UN_ATOM(cb2) == ATOM_TRUE))
                goal_stack[goal_sp++] = cb2;
        }
        continue;
    }
}
