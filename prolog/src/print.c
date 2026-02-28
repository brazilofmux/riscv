#include "term.h"
#include "engine.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int needs_quoting(const char *s) {
    if (!s || !s[0]) return 1;
    /* If starts with uppercase or _, needs quoting */
    if (isupper(s[0]) || s[0] == '_') return 1;
    /* If starts with lowercase, check all chars are alnum/_ */
    if (islower(s[0])) {
        int i;
        for (i = 0; s[i]; i++) {
            if (!isalnum(s[i]) && s[i] != '_') return 1;
        }
        return 0;
    }
    /* Operator atoms don't need quoting */
    return 0;
}

static int quote_mode = 0; /* 0 = write (no quotes), 1 = writeq */

static void write_atom(int atom_id) {
    const char *name = atom_name(atom_id);
    if (quote_mode && needs_quoting(name)) {
        putchar('\'');
        while (*name) {
            if (*name == '\'') putchar('\'');
            putchar(*name);
            name++;
        }
        putchar('\'');
    } else {
        printf("%s", name);
    }
}

static void write_term_depth(term_t t, int depth, int in_list);

static void write_list(term_t t, int depth) {
    /* t is a ./2 compound */
    term_t head = compound_arg(t, 0);
    term_t tail = deref(compound_arg(t, 1));

    write_term_depth(head, depth + 1, 0);

    if (TAG(tail) == TAG_ATOM && UN_ATOM(tail) == ATOM_NIL_LIST) {
        /* End of proper list */
        return;
    }
    if (IS_PTR(tail) && compound_functor(tail) == ATOM_DOT && compound_arity(tail) == 2) {
        /* More list elements */
        putchar(',');
        write_list(tail, depth);
        return;
    }
    /* Improper list */
    putchar('|');
    write_term_depth(tail, depth + 1, 0);
}

static int is_infix_op(int atom_id) {
    return atom_id == ATOM_COMMA || atom_id == ATOM_SEMI ||
           atom_id == ATOM_ARROW || atom_id == ATOM_PLUS ||
           atom_id == ATOM_MINUS || atom_id == ATOM_STAR ||
           atom_id == ATOM_SLASH2 || atom_id == ATOM_MOD ||
           atom_id == ATOM_IS || atom_id == ATOM_UNIFY ||
           atom_id == ATOM_NOT_UNIFY || atom_id == ATOM_EQ ||
           atom_id == ATOM_NEQ || atom_id == ATOM_LT ||
           atom_id == ATOM_GT || atom_id == ATOM_LE ||
           atom_id == ATOM_GE || atom_id == ATOM_ARITH_EQ ||
           atom_id == ATOM_ARITH_NEQ || atom_id == ATOM_CLAUSE ||
           atom_id == ATOM_UNIV;
}

static void write_term_depth(term_t t, int depth, int in_list) {
    if (depth > 100) {
        printf("...");
        return;
    }

    t = deref(t);

    if (t == TERM_NIL) {
        printf("nil");
        return;
    }

    switch (TAG(t)) {
    case TAG_INT:
        printf("%d", (int)UN_INT(t));
        break;

    case TAG_ATOM: {
        int id = UN_ATOM(t);
        if (id == ATOM_NIL_LIST) {
            printf("[]");
        } else {
            write_atom(id);
        }
        break;
    }

    case TAG_VAR: {
        int id = UN_VAR(t);
        term_t val = var_binding[id];
        if (val != TERM_NIL) {
            write_term_depth(val, depth + 1, in_list);
        } else {
            printf("_G%d", id);
        }
        break;
    }

    default: /* TAG_PTR */
        if (!IS_PTR(t)) {
            printf("nil");
            break;
        }
        {
            int func = compound_functor(t);
            int arity = compound_arity(t);

            /* List sugar */
            if (func == ATOM_DOT && arity == 2) {
                putchar('[');
                write_list(t, depth);
                putchar(']');
                break;
            }

            /* Infix operators */
            if (arity == 2 && is_infix_op(func)) {
                term_t left = compound_arg(t, 0);
                term_t right = compound_arg(t, 1);
                if (func == ATOM_COMMA) {
                    write_term_depth(left, depth + 1, 0);
                    putchar(',');
                    write_term_depth(right, depth + 1, 0);
                } else {
                    write_term_depth(left, depth + 1, 0);
                    printf("%s", atom_name(func));
                    write_term_depth(right, depth + 1, 0);
                }
                break;
            }

            /* Prefix operators */
            if (arity == 1 && (func == ATOM_NOT || func == ATOM_MINUS)) {
                printf("%s", atom_name(func));
                write_term_depth(compound_arg(t, 0), depth + 1, 0);
                break;
            }

            /* Standard compound: f(a1, a2, ...) */
            write_atom(func);
            putchar('(');
            int i;
            for (i = 0; i < arity; i++) {
                if (i > 0) putchar(',');
                write_term_depth(compound_arg(t, i), depth + 1, 0);
            }
            putchar(')');
        }
        break;
    }
}

void write_term(term_t t) {
    write_term_depth(t, 0, 0);
}

void writeln_term(term_t t) {
    write_term(t);
    putchar('\n');
}
