#include "parser.h"
#include "reader.h"
#include "engine.h"
#include <string.h>
#include <stdio.h>

clause_var_t clause_vars[MAX_CLAUSE_VARS];
int clause_var_count = 0;
int clause_max_var = 0;

void parser_init(void) {
    clause_var_count = 0;
    clause_max_var = 0;
}

static int lookup_var(const char *name) {
    /* _ is always fresh */
    if (name[0] == '_' && name[1] == 0) {
        int id = clause_max_var++;
        return id;
    }
    int i;
    for (i = 0; i < clause_var_count; i++) {
        if (strcmp(clause_vars[i].name, name) == 0)
            return clause_vars[i].var_id;
    }
    if (clause_var_count >= MAX_CLAUSE_VARS) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "too many variables in clause");
        return 0;
    }
    int id = clause_max_var++;
    strncpy(clause_vars[clause_var_count].name, name, 63);
    clause_vars[clause_var_count].name[63] = 0;
    clause_vars[clause_var_count].var_id = id;
    clause_vars[clause_var_count].is_named = !(name[0] == '_');
    clause_var_count++;
    return id;
}

/* Forward declarations */
static term_t parse_expr(int max_prec);
static term_t parse_primary(void);
static term_t parse_list_tail(void);

/* Operator precedence and type */
typedef struct {
    int atom_id;
    int prec;
    int type; /* 0=xfx, 1=xfy, 2=yfx, 3=fx, 4=fy */
} op_info_t;

#define MAX_OPS 64
static op_info_t ops[MAX_OPS];
static int op_count = 0;

static void add_op(const char *name, int prec, int type) {
    if (op_count >= MAX_OPS) return;
    ops[op_count].atom_id = atom_intern(name);
    ops[op_count].prec = prec;
    ops[op_count].type = type;
    op_count++;
}

void init_operators(void) {
    op_count = 0;
    add_op(":-", 1200, 0);  /* xfx */
    add_op("?-", 1200, 3);  /* fx */
    add_op(";", 1100, 1);   /* xfy */
    add_op("->", 1050, 1);  /* xfy */
    add_op(",", 1000, 1);   /* xfy */
    add_op("\\+", 900, 4);  /* fy */
    add_op("is", 700, 0);   /* xfx */
    add_op("=", 700, 0);
    add_op("\\=", 700, 0);
    add_op("==", 700, 0);
    add_op("\\==", 700, 0);
    add_op("<", 700, 0);
    add_op(">", 700, 0);
    add_op("=<", 700, 0);
    add_op(">=", 700, 0);
    add_op("=:=", 700, 0);
    add_op("=\\=", 700, 0);
    add_op("=..", 700, 0);
    add_op("+", 500, 2);    /* yfx */
    add_op("-", 500, 2);    /* yfx */
    add_op("*", 400, 2);    /* yfx */
    add_op("//", 400, 2);   /* yfx */
    add_op("mod", 400, 2);  /* yfx */
}

/* Find infix operator with prec <= max_prec */
static op_info_t *find_infix_op(int atom_id, int max_prec) {
    int i;
    for (i = 0; i < op_count; i++) {
        if (ops[i].atom_id == atom_id && ops[i].type <= 2 && ops[i].prec <= max_prec)
            return &ops[i];
    }
    return 0;
}

/* Find prefix operator */
static op_info_t *find_prefix_op(int atom_id) {
    int i;
    for (i = 0; i < op_count; i++) {
        if (ops[i].atom_id == atom_id && (ops[i].type == 3 || ops[i].type == 4))
            return &ops[i];
    }
    return 0;
}

static term_t parse_list(void) {
    /* Already consumed '[' */
    if (cur_tok.type == TOK_RBRACKET) {
        next_token();
        return MK_ATOM(ATOM_NIL_LIST);
    }

    term_t head = parse_expr(999);
    if (g_error) return TERM_NIL;

    if (cur_tok.type == TOK_BAR) {
        next_token();
        term_t tail = parse_expr(999);
        if (g_error) return TERM_NIL;
        if (cur_tok.type != TOK_RBRACKET) {
            g_error = 1;
            snprintf(g_errmsg, sizeof(g_errmsg), "expected ']' after list tail");
            return TERM_NIL;
        }
        next_token();
        return make_list_cons(head, tail);
    }

    if (cur_tok.type == TOK_COMMA) {
        next_token();
        term_t rest = parse_list_tail();
        return make_list_cons(head, rest);
    }

    if (cur_tok.type == TOK_RBRACKET) {
        next_token();
        return make_list_cons(head, MK_ATOM(ATOM_NIL_LIST));
    }

    g_error = 1;
    snprintf(g_errmsg, sizeof(g_errmsg), "expected ',', '|', or ']' in list");
    return TERM_NIL;
}

static term_t parse_list_tail(void) {
    term_t head = parse_expr(999);
    if (g_error) return TERM_NIL;

    if (cur_tok.type == TOK_BAR) {
        next_token();
        term_t tail = parse_expr(999);
        if (g_error) return TERM_NIL;
        if (cur_tok.type != TOK_RBRACKET) {
            g_error = 1;
            snprintf(g_errmsg, sizeof(g_errmsg), "expected ']'");
            return TERM_NIL;
        }
        next_token();
        return make_list_cons(head, tail);
    }

    if (cur_tok.type == TOK_COMMA) {
        next_token();
        term_t rest = parse_list_tail();
        return make_list_cons(head, rest);
    }

    if (cur_tok.type == TOK_RBRACKET) {
        next_token();
        return make_list_cons(head, MK_ATOM(ATOM_NIL_LIST));
    }

    g_error = 1;
    snprintf(g_errmsg, sizeof(g_errmsg), "unexpected token in list");
    return TERM_NIL;
}

static term_t parse_args(int functor_atom) {
    /* Already consumed '(' */
    term_t args[PROLOG_MAX_ARITY];
    int arity = 0;

    args[arity++] = parse_expr(999);
    if (g_error) return TERM_NIL;

    while (cur_tok.type == TOK_COMMA) {
        next_token();
        if (arity >= PROLOG_MAX_ARITY) {
            g_error = 1;
            snprintf(g_errmsg, sizeof(g_errmsg), "too many arguments (max %d)", PROLOG_MAX_ARITY);
            return TERM_NIL;
        }
        args[arity++] = parse_expr(999);
        if (g_error) return TERM_NIL;
    }

    if (cur_tok.type != TOK_RPAREN) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "expected ')'");
        return TERM_NIL;
    }
    next_token();

    return make_compound(functor_atom, arity, args);
}

static term_t parse_primary(void) {
    if (g_error) return TERM_NIL;

    /* Parenthesized expression */
    if (cur_tok.type == TOK_LPAREN) {
        next_token();
        term_t t = parse_expr(1200);
        if (cur_tok.type != TOK_RPAREN) {
            g_error = 1;
            snprintf(g_errmsg, sizeof(g_errmsg), "expected ')'");
            return TERM_NIL;
        }
        next_token();
        return t;
    }

    /* List */
    if (cur_tok.type == TOK_LBRACKET) {
        next_token();
        return parse_list();
    }

    /* Variable */
    if (cur_tok.type == TOK_VAR) {
        int id = lookup_var(cur_tok.text);
        next_token();
        return MK_VAR(id);
    }

    /* Integer */
    if (cur_tok.type == TOK_INT) {
        term_t t = MK_INT(cur_tok.int_val);
        next_token();
        return t;
    }

    /* String (as atom) */
    if (cur_tok.type == TOK_STRING) {
        int id = atom_intern(cur_tok.text);
        next_token();
        return MK_ATOM(id);
    }

    /* Atom - possibly followed by '(' for compound */
    if (cur_tok.type == TOK_ATOM) {
        int atom_id = atom_intern(cur_tok.text);
        next_token();

        /* Check for prefix operator */
        op_info_t *pfx = find_prefix_op(atom_id);

        /* Atom followed by '(' → compound term */
        if (cur_tok.type == TOK_LPAREN) {
            next_token();
            return parse_args(atom_id);
        }

        /* Prefix operator (but not '-' which is handled specially for negative ints) */
        if (pfx) {
            int rp = (pfx->type == 3) ? pfx->prec - 1 : pfx->prec; /* fx vs fy */
            term_t arg = parse_expr(rp);
            if (g_error) return TERM_NIL;
            term_t args[1];
            args[0] = arg;
            return make_compound(atom_id, 1, args);
        }

        return MK_ATOM(atom_id);
    }

    if (cur_tok.type == TOK_EOF) {
        return TERM_NIL;
    }

    g_error = 1;
    snprintf(g_errmsg, sizeof(g_errmsg), "unexpected token");
    return TERM_NIL;
}

static term_t parse_expr(int max_prec) {
    term_t left = parse_primary();
    if (g_error) return TERM_NIL;

    for (;;) {
        if (cur_tok.type != TOK_ATOM && cur_tok.type != TOK_COMMA)
            break;

        int atom_id;
        if (cur_tok.type == TOK_COMMA) {
            atom_id = ATOM_COMMA;
        } else {
            atom_id = atom_intern(cur_tok.text);
        }

        op_info_t *op = find_infix_op(atom_id, max_prec);
        if (!op) break;

        next_token();

        int rp;
        if (op->type == 0) rp = op->prec - 1;       /* xfx */
        else if (op->type == 1) rp = op->prec;       /* xfy */
        else rp = op->prec - 1;                       /* yfx */

        term_t right = parse_expr(rp);
        if (g_error) return TERM_NIL;

        term_t args[2];
        args[0] = left;
        args[1] = right;
        left = make_compound(atom_id, 2, args);
    }

    return left;
}

term_t parse_term(void) {
    parser_init();
    next_token();
    if (cur_tok.type == TOK_EOF) return TERM_NIL;

    term_t t = parse_expr(1200);
    if (g_error) return TERM_NIL;

    if (cur_tok.type != TOK_DOT) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "expected '.' at end of term");
        return TERM_NIL;
    }
    /* Parsed vars are clause-local [0..clause_max_var). */
    var_count = clause_max_var;
    /* DOT consumed */
    return t;
}
