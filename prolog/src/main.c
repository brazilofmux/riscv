#include "term.h"
#include "engine.h"
#include "database.h"
#include "reader.h"
#include "parser.h"
#include <stdio.h>
#include <string.h>

/* From parser.c */
extern void init_operators(void);

/* From print.c */
extern void write_term(term_t t);

static void print_bindings(void) {
    int i;
    int found = 0;
    for (i = 0; i < clause_var_count; i++) {
        if (!clause_vars[i].is_named) continue;
        term_t val = deref(MK_VAR(clause_vars[i].var_id));
        if (TAG(val) == TAG_VAR) {
            int vid = UN_VAR(val);
            if (vid >= 0 && vid < var_count && var_binding[vid] == TERM_NIL)
                continue; /* Unbound (possibly aliased) */
        }
        if (found) printf(",");
        printf("%s = ", clause_vars[i].name);
        write_term(val);
        found = 1;
    }
    if (found)
        printf("\n");
}

int main(void) {
    init_atoms();
    db_init();
    reader_init();
    init_operators();

    for (;;) {
        g_error = 0;

        /* Reset working heap and vars between top-level terms */
        engine_reset();

        term_t t = parse_term();

        if (g_error) {
            printf("Error: %s\n", g_errmsg);
            g_error = 0;
            continue;
        }

        if (t == TERM_NIL) break; /* EOF */

        t = deref(t);

        /* Directive: :- Goal */
        if (IS_PTR(t) && compound_functor(t) == ATOM_CLAUSE && compound_arity(t) == 1) {
            term_t goal = compound_arg(t, 0);
            int saved_vc = var_count;
            int saved_hp = hp;
            if (!solve(goal)) {
                printf("Error: directive failed\n");
            }
            g_error = 0;
            continue;
        }

        /* Query: ?- Goal */
        if (IS_PTR(t) && compound_functor(t) == ATOM_QUERY && compound_arity(t) == 1) {
            term_t goal = compound_arg(t, 0);

            /* Save clause var info for binding printout */
            int saved_cvc = clause_var_count;
            clause_var_t saved_cvars[MAX_CLAUSE_VARS];
            memcpy(saved_cvars, clause_vars, clause_var_count * sizeof(clause_var_t));

            if (solve(goal)) {
                /* Restore var info for printing */
                clause_var_count = saved_cvc;
                memcpy(clause_vars, saved_cvars, saved_cvc * sizeof(clause_var_t));
                print_bindings();
                printf("true.\n");
            } else {
                if (g_error) {
                    printf("Error: %s\n", g_errmsg);
                    g_error = 0;
                } else {
                    printf("false.\n");
                }
            }
            continue;
        }

        /* Clause: Head :- Body, or just Head (fact) */
        if (IS_PTR(t) && compound_functor(t) == ATOM_CLAUSE && compound_arity(t) == 2) {
            term_t head = compound_arg(t, 0);
            term_t body = compound_arg(t, 1);
            db_add_clause(head, body, clause_max_var, 0);
            if (g_error) {
                printf("Error: %s\n", g_errmsg);
                g_error = 0;
            }
            continue;
        }

        /* Bare fact */
        db_add_clause(t, MK_ATOM(ATOM_TRUE), clause_max_var, 0);
        if (g_error) {
            printf("Error: %s\n", g_errmsg);
            g_error = 0;
        }
    }

    return 0;
}
