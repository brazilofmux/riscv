/* ast.c — Compile expressions to AST, evaluate AST trees.
 *
 * The compile phase mirrors expr.c's recursive descent parser exactly,
 * but builds tree nodes instead of computing values.  The eval phase
 * walks the tree, using the same type-checking and operator semantics
 * as the original parser.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include "ast.h"
#include "memvar.h"
#include "set.h"
#include "date.h"
#include "util.h"
#include "lex.h"

/* Forward declaration for func.c */
int func_call(expr_ctx_t *ctx, const char *name, value_t *args, int nargs, value_t *result);

/* ================================================================
 *  Node constructors
 * ================================================================ */

static ast_node_t *node_alloc(ast_type_t type) {
    ast_node_t *n = calloc(1, sizeof(ast_node_t));
    if (n) n->type = type;
    return n;
}

static ast_node_t *node_literal(value_t val) {
    ast_node_t *n = node_alloc(AST_LITERAL);
    if (n) n->literal = val;
    return n;
}

static ast_node_t *node_memvar(const char *name) {
    ast_node_t *n = node_alloc(AST_MEMVAR);
    if (n) str_copy(n->name, name, sizeof(n->name));
    return n;
}

static ast_node_t *node_alias_field(const char *alias, const char *field) {
    ast_node_t *n = node_alloc(AST_ALIAS_FIELD);
    if (n) {
        str_copy(n->alias_field.alias, alias, sizeof(n->alias_field.alias));
        str_copy(n->alias_field.field, field, sizeof(n->alias_field.field));
    }
    return n;
}

static ast_node_t *node_array(const char *name, ast_node_t *idx1, ast_node_t *idx2) {
    ast_node_t *n = node_alloc(AST_ARRAY_ACCESS);
    if (n) {
        str_copy(n->array.name, name, sizeof(n->array.name));
        n->array.index1 = idx1;
        n->array.index2 = idx2;
    }
    return n;
}

static ast_node_t *node_binop(ast_op_t op, ast_node_t *left, ast_node_t *right) {
    ast_node_t *n = node_alloc(AST_BINOP);
    if (n) {
        n->binop.op = op;
        n->binop.left = left;
        n->binop.right = right;
    }
    return n;
}

static ast_node_t *node_unop(ast_op_t op, ast_node_t *child) {
    ast_node_t *n = node_alloc(AST_UNOP);
    if (n) {
        n->unop.op = op;
        n->unop.child = child;
    }
    return n;
}

static ast_node_t *node_call(const char *name, ast_node_t **args, int nargs) {
    ast_node_t *n = node_alloc(AST_FUNC_CALL);
    if (n) {
        str_copy(n->call.name, name, sizeof(n->call.name));
        n->call.args = args;
        n->call.nargs = nargs;
    }
    return n;
}

/* ================================================================
 *  ast_free — recursive
 * ================================================================ */

void ast_free(ast_node_t *node) {
    int i;
    if (!node) return;

    switch (node->type) {
    case AST_LITERAL:
    case AST_FIELD:
    case AST_MEMVAR:
        break;
    case AST_ALIAS_FIELD:
        break;
    case AST_ARRAY_ACCESS:
        ast_free(node->array.index1);
        ast_free(node->array.index2);
        break;
    case AST_BINOP:
        ast_free(node->binop.left);
        ast_free(node->binop.right);
        break;
    case AST_UNOP:
        ast_free(node->unop.child);
        break;
    case AST_FUNC_CALL:
        for (i = 0; i < node->call.nargs; i++)
            ast_free(node->call.args[i]);
        free(node->call.args);
        break;
    }
    free(node);
}

/* ================================================================
 *  Compile phase — recursive descent → tree
 * ================================================================ */

/* Forward declarations */
static ast_node_t *ast_parse_expr(lexer_t *l, memvar_store_t *store, const char **error);
static ast_node_t *ast_parse_or(lexer_t *l, memvar_store_t *store, const char **error);
static ast_node_t *ast_parse_and(lexer_t *l, memvar_store_t *store, const char **error);
static ast_node_t *ast_parse_not(lexer_t *l, memvar_store_t *store, const char **error);
static ast_node_t *ast_parse_compare(lexer_t *l, memvar_store_t *store, const char **error);
static ast_node_t *ast_parse_add(lexer_t *l, memvar_store_t *store, const char **error);
static ast_node_t *ast_parse_mul(lexer_t *l, memvar_store_t *store, const char **error);
static ast_node_t *ast_parse_power(lexer_t *l, memvar_store_t *store, const char **error);
static ast_node_t *ast_parse_unary(lexer_t *l, memvar_store_t *store, const char **error);
static ast_node_t *ast_parse_primary(lexer_t *l, memvar_store_t *store, const char **error);

/* expr → or_expr */
static ast_node_t *ast_parse_expr(lexer_t *l, memvar_store_t *store, const char **error) {
    return ast_parse_or(l, store, error);
}

/* or_expr → and_expr [ .OR. and_expr ]* */
static ast_node_t *ast_parse_or(lexer_t *l, memvar_store_t *store, const char **error) {
    ast_node_t *left = ast_parse_and(l, store, error);
    if (!left) return NULL;

    while (lex_peek(l) == TOK_OR) {
        ast_node_t *right;
        lex_next(l);
        right = ast_parse_and(l, store, error);
        if (!right) { ast_free(left); return NULL; }
        left = node_binop(OP_OR, left, right);
        if (!left) { *error = "Out of memory"; return NULL; }
    }
    return left;
}

/* and_expr → not_expr [ .AND. not_expr ]* */
static ast_node_t *ast_parse_and(lexer_t *l, memvar_store_t *store, const char **error) {
    ast_node_t *left = ast_parse_not(l, store, error);
    if (!left) return NULL;

    while (lex_peek(l) == TOK_AND) {
        ast_node_t *right;
        lex_next(l);
        right = ast_parse_not(l, store, error);
        if (!right) { ast_free(left); return NULL; }
        left = node_binop(OP_AND, left, right);
        if (!left) { *error = "Out of memory"; return NULL; }
    }
    return left;
}

/* not_expr → [.NOT.] compare */
static ast_node_t *ast_parse_not(lexer_t *l, memvar_store_t *store, const char **error) {
    if (lex_peek(l) == TOK_NOT) {
        ast_node_t *child;
        lex_next(l);
        child = ast_parse_compare(l, store, error);
        if (!child) return NULL;
        return node_unop(OP_NOT, child);
    }
    return ast_parse_compare(l, store, error);
}

/* compare → add_expr [ (= | == | <> | # | < | > | <= | >= | $) add_expr ] */
static ast_node_t *ast_parse_compare(lexer_t *l, memvar_store_t *store, const char **error) {
    ast_node_t *left = ast_parse_add(l, store, error);
    if (!left) return NULL;

    {
        token_type_t op = lex_peek(l);
        ast_op_t ast_op;
        ast_node_t *right;

        switch (op) {
        case TOK_EQ:       ast_op = OP_EQ;       break;
        case TOK_EXACT_EQ: ast_op = OP_EXACT_EQ; break;
        case TOK_NE:       ast_op = OP_NE;       break;
        case TOK_LT:       ast_op = OP_LT;       break;
        case TOK_GT:       ast_op = OP_GT;       break;
        case TOK_LE:       ast_op = OP_LE;       break;
        case TOK_GE:       ast_op = OP_GE;       break;
        case TOK_SUBSTR:   ast_op = OP_SUBSTR;   break;
        default:           return left;  /* no comparison operator */
        }

        lex_next(l);
        right = ast_parse_add(l, store, error);
        if (!right) { ast_free(left); return NULL; }
        left = node_binop(ast_op, left, right);
        if (!left) { *error = "Out of memory"; return NULL; }
    }
    return left;
}

/* add_expr → mul_expr [ (+ | -) mul_expr ]* */
static ast_node_t *ast_parse_add(lexer_t *l, memvar_store_t *store, const char **error) {
    ast_node_t *left = ast_parse_mul(l, store, error);
    if (!left) return NULL;

    for (;;) {
        token_type_t op = lex_peek(l);
        ast_op_t ast_op;
        ast_node_t *right;

        if (op == TOK_PLUS)       ast_op = OP_ADD;
        else if (op == TOK_MINUS) ast_op = OP_SUB;
        else break;

        lex_next(l);
        right = ast_parse_mul(l, store, error);
        if (!right) { ast_free(left); return NULL; }
        left = node_binop(ast_op, left, right);
        if (!left) { *error = "Out of memory"; return NULL; }
    }
    return left;
}

/* mul_expr → power_expr [ (* | /) power_expr ]* */
static ast_node_t *ast_parse_mul(lexer_t *l, memvar_store_t *store, const char **error) {
    ast_node_t *left = ast_parse_power(l, store, error);
    if (!left) return NULL;

    for (;;) {
        token_type_t op = lex_peek(l);
        ast_op_t ast_op;
        ast_node_t *right;

        if (op == TOK_MUL)       ast_op = OP_MUL;
        else if (op == TOK_DIV)  ast_op = OP_DIV;
        else break;

        lex_next(l);
        right = ast_parse_power(l, store, error);
        if (!right) { ast_free(left); return NULL; }
        left = node_binop(ast_op, left, right);
        if (!left) { *error = "Out of memory"; return NULL; }
    }
    return left;
}

/* power_expr → unary [ (** | ^) unary ] */
static ast_node_t *ast_parse_power(lexer_t *l, memvar_store_t *store, const char **error) {
    ast_node_t *left = ast_parse_unary(l, store, error);
    if (!left) return NULL;

    if (lex_peek(l) == TOK_POWER) {
        ast_node_t *right;
        lex_next(l);
        right = ast_parse_unary(l, store, error);
        if (!right) { ast_free(left); return NULL; }
        left = node_binop(OP_POWER, left, right);
        if (!left) { *error = "Out of memory"; return NULL; }
    }
    return left;
}

/* unary → [-|+] primary */
static ast_node_t *ast_parse_unary(lexer_t *l, memvar_store_t *store, const char **error) {
    token_type_t op = lex_peek(l);

    if (op == TOK_MINUS) {
        ast_node_t *child;
        lex_next(l);
        child = ast_parse_primary(l, store, error);
        if (!child) return NULL;
        return node_unop(OP_NEGATE, child);
    }

    if (op == TOK_PLUS) {
        lex_next(l);
    }

    return ast_parse_primary(l, store, error);
}

/* primary → number | string | .T./.F. | {date} | (expr) | func(args) |
 *           alias->field | array[i] | name */
static ast_node_t *ast_parse_primary(lexer_t *l, memvar_store_t *store, const char **error) {
    token_t t = l->current;

    switch (t.type) {
    case TOK_NUMBER:
        lex_next(l);
        return node_literal(val_num(t.num_val));

    case TOK_STRING:
        lex_next(l);
        return node_literal(val_str(t.text));

    case TOK_LOGIC:
        lex_next(l);
        return node_literal(val_logic(t.logic_val));

    case TOK_DATE:
        lex_next(l);
        return node_literal(val_date(t.date_val));

    case TOK_MACRO:
        *error = "Expected identifier after &";
        return NULL;

    case TOK_LPAREN: {
        ast_node_t *inner;
        lex_next(l);
        inner = ast_parse_expr(l, store, error);
        if (!inner) return NULL;
        if (lex_peek(l) != TOK_RPAREN) {
            *error = "Missing closing parenthesis";
            ast_free(inner);
            return NULL;
        }
        lex_next(l);
        return inner;
    }

    case TOK_IDENT: {
        char name[64];
        str_copy(name, t.text, sizeof(name));
        lex_next(l);

        /* Check for array indexed access: name[i] or name(i) */
        {
            int is_array = 0;
            if (store) {
                value_t mv;
                if (memvar_find(store, name, &mv) == 0 && mv.type == VAL_ARRAY)
                    is_array = 1;
            }

            if ((is_array && (l->current.type == TOK_LPAREN || l->current.type == TOK_LBRACKET)) ||
                (l->current.type == TOK_LBRACKET)) {
                token_type_t end_type = (l->current.type == TOK_LPAREN) ? TOK_RPAREN : TOK_RBRACKET;
                ast_node_t *idx1, *idx2 = NULL;

                lex_next(l);  /* skip [ or ( */
                idx1 = ast_parse_expr(l, store, error);
                if (!idx1) return NULL;

                if (lex_peek(l) == TOK_COMMA) {
                    lex_next(l);
                    idx2 = ast_parse_expr(l, store, error);
                    if (!idx2) { ast_free(idx1); return NULL; }
                }

                if (lex_peek(l) == end_type) {
                    lex_next(l);
                }

                return node_array(name, idx1, idx2);
            }
        }

        /* Check for alias->field reference */
        if (lex_peek(l) == TOK_ARROW) {
            char field_name[64];
            lex_next(l);
            if (lex_peek(l) != TOK_IDENT) {
                *error = "Expected field name after ->";
                return NULL;
            }
            str_copy(field_name, l->current.text, sizeof(field_name));
            lex_next(l);
            return node_alias_field(name, field_name);
        }

        /* Function call: name( */
        if (lex_peek(l) == TOK_LPAREN) {
            ast_node_t **args = NULL;
            int nargs = 0;
            int capacity = 0;

            lex_next(l); /* skip ( */

            if (lex_peek(l) != TOK_RPAREN) {
                for (;;) {
                    ast_node_t *arg = ast_parse_expr(l, store, error);
                    if (!arg) {
                        int j;
                        for (j = 0; j < nargs; j++) ast_free(args[j]);
                        free(args);
                        return NULL;
                    }
                    if (nargs >= MAX_FUNC_ARGS) {
                        int j;
                        *error = "Too many function arguments";
                        for (j = 0; j < nargs; j++) ast_free(args[j]);
                        free(args);
                        ast_free(arg);
                        return NULL;
                    }
                    if (nargs >= capacity) {
                        ast_node_t **new_args;
                        capacity = capacity ? capacity * 2 : 4;
                        new_args = realloc(args, capacity * sizeof(ast_node_t *));
                        if (!new_args) {
                            int j;
                            *error = "Out of memory";
                            for (j = 0; j < nargs; j++) ast_free(args[j]);
                            free(args);
                            ast_free(arg);
                            return NULL;
                        }
                        args = new_args;
                    }
                    args[nargs++] = arg;
                    if (lex_peek(l) == TOK_COMMA) { lex_next(l); continue; }
                    break;
                }
            }

            if (lex_peek(l) != TOK_RPAREN) {
                int j;
                *error = "Missing ) in function call";
                for (j = 0; j < nargs; j++) ast_free(args[j]);
                free(args);
                return NULL;
            }
            lex_next(l);

            return node_call(name, args, nargs);
        }

        /* Bare identifier → resolve at eval time (field first, then memvar) */
        return node_memvar(name);
    }

    default:
        *error = "Unexpected token in expression";
        return NULL;
    }
}

/* ================================================================
 *  Public compile API
 * ================================================================ */

ast_node_t *ast_compile(const char *expr, memvar_store_t *store, const char **error) {
    lexer_t l;
    lexer_init_ext(&l, expr, store);
    *error = NULL;
    return ast_parse_expr(&l, store, error);
}

ast_node_t *ast_compile_adv(const char **pp, memvar_store_t *store, const char **error) {
    lexer_t l;
    ast_node_t *node;
    lexer_init_ext(&l, *pp, store);
    *error = NULL;
    node = ast_parse_expr(&l, store, error);
    *pp = l.token_start;
    return node;
}

/* ================================================================
 *  Eval phase — tree walk
 * ================================================================ */

static int eval_field_from_db(dbf_t *db, const char *name, value_t *result) {
    int idx;
    if (!db || !dbf_is_open(db) || db->current_record == 0) return -1;
    idx = dbf_find_field(db, name);
    if (idx < 0) return -1;
    {
        char raw[256];
        dbf_get_field_raw(db, idx, raw, sizeof(raw));
        switch (db->fields[idx].type) {
        case 'C': *result = val_str(raw); return 0;
        case 'F':
        case 'N': *result = val_num(atof(raw)); return 0;
        case 'D': *result = val_date(date_from_dbf(raw)); return 0;
        case 'L': *result = val_logic(raw[0] == 'T' || raw[0] == 't'); return 0;
        case 'M': {
            int blk = atoi(raw);
            char memo[256];
            if (blk > 0 && dbf_memo_read(db, blk, memo, sizeof(memo)) == 0)
                *result = val_str(memo);
            else
                *result = val_str("");
            return 0;
        }
        }
    }
    return -1;
}

int ast_eval(ast_node_t *node, expr_ctx_t *ctx, value_t *result) {
    if (!node) { ctx->error = "NULL AST node"; return -1; }

    switch (node->type) {

    case AST_LITERAL:
        *result = node->literal;
        return 0;

    case AST_FIELD:
        if (eval_field_from_db(ctx->db, node->name, result) == 0) return 0;
        snprintf(ctx->err_msg, sizeof(ctx->err_msg), "Field not found: %s", node->name);
        ctx->error = ctx->err_msg;
        return -1;

    case AST_MEMVAR:
        /* Field-first lookup, then memvar — matches parse_primary in expr.c */
        if (eval_field_from_db(ctx->db, node->name, result) == 0) return 0;
        if (ctx->vars) {
            value_t mv;
            if (memvar_find(ctx->vars, node->name, &mv) == 0) {
                *result = mv;
                return 0;
            }
        }
        snprintf(ctx->err_msg, sizeof(ctx->err_msg), "Variable not found: %s", node->name);
        ctx->error = ctx->err_msg;
        return -1;

    case AST_ALIAS_FIELD:
        if (ctx->area_lookup) {
            dbf_t *adb = ctx->area_lookup(node->alias_field.alias);
            if (eval_field_from_db(adb, node->alias_field.field, result) == 0) return 0;
        }
        snprintf(ctx->err_msg, sizeof(ctx->err_msg), "Cannot resolve %s->%s",
                 node->alias_field.alias, node->alias_field.field);
        ctx->error = ctx->err_msg;
        return -1;

    case AST_ARRAY_ACCESS: {
        value_t row_v, col_v;
        int row, col = 1, res;

        if (ast_eval(node->array.index1, ctx, &row_v) != 0) return -1;
        if (row_v.type != VAL_NUM) { ctx->error = "Array index must be numeric"; return -1; }
        row = (int)row_v.num;

        if (node->array.index2) {
            if (ast_eval(node->array.index2, ctx, &col_v) != 0) return -1;
            if (col_v.type != VAL_NUM) { ctx->error = "Array index must be numeric"; return -1; }
            col = (int)col_v.num;
        }

        res = memvar_get_elem(ctx->vars, node->array.name, row, col, result);
        if (res == 0) return 0;
        if (res == -2) { ctx->error = "Array index out of bounds"; return -1; }
        ctx->error = "Variable is not an array";
        return -1;
    }

    case AST_FUNC_CALL: {
        value_t *args = NULL;
        int i;
        int rc;

        if (node->call.nargs > MAX_FUNC_ARGS) {
            ctx->error = "Too many function arguments";
            return -1;
        }
        if (node->call.nargs > 0) {
            args = (value_t *)malloc(node->call.nargs * sizeof(value_t));
            if (!args) {
                ctx->error = "Out of memory";
                return -1;
            }
        }
        for (i = 0; i < node->call.nargs; i++) {
            if (ast_eval(node->call.args[i], ctx, &args[i]) != 0) {
                if (args) free(args);
                return -1;
            }
        }
        rc = func_call(ctx, node->call.name, args, node->call.nargs, result);
        if (args) free(args);
        return rc;
    }

    case AST_UNOP: {
        value_t child;
        if (ast_eval(node->unop.child, ctx, &child) != 0) return -1;

        if (node->unop.op == OP_NEGATE) {
            if (child.type != VAL_NUM) { ctx->error = "Unary minus requires number"; return -1; }
            *result = val_num(-child.num);
            return 0;
        }
        if (node->unop.op == OP_NOT) {
            if (child.type != VAL_LOGIC) { ctx->error = "Type mismatch in .NOT."; return -1; }
            *result = val_logic(!child.logic);
            return 0;
        }
        ctx->error = "Unknown unary operator";
        return -1;
    }

    case AST_BINOP: {
        value_t left, right;
        if (ast_eval(node->binop.left, ctx, &left) != 0) return -1;
        if (ast_eval(node->binop.right, ctx, &right) != 0) return -1;

        switch (node->binop.op) {

        /* ---- Arithmetic ---- */
        case OP_ADD:
            if (left.type == VAL_NUM && right.type == VAL_NUM) {
                *result = val_num(left.num + right.num);
            } else if (left.type == VAL_CHAR && right.type == VAL_CHAR) {
                /* String concatenation (truncate if too long) */
                char buf[256];
                str_copy(buf, left.str, sizeof(buf));
                {
                    int len = strlen(buf);
                    int rlen = strlen(right.str);
                    int avail = (int)sizeof(buf) - 1 - len;
                    if (avail > 0) {
                        if (rlen > avail) rlen = avail;
                        memcpy(buf + len, right.str, rlen);
                        buf[len + rlen] = '\0';
                    }
                }
                *result = val_str(buf);
            } else if (left.type == VAL_DATE && right.type == VAL_NUM) {
                *result = val_date(left.date + (int32_t)right.num);
            } else if (left.type == VAL_NUM && right.type == VAL_DATE) {
                *result = val_date(right.date + (int32_t)left.num);
            } else {
                ctx->error = "Type mismatch in +"; return -1;
            }
            return 0;

        case OP_SUB:
            if (left.type == VAL_NUM && right.type == VAL_NUM) {
                *result = val_num(left.num - right.num);
            } else if (left.type == VAL_DATE && right.type == VAL_DATE) {
                *result = val_num((double)(left.date - right.date));
            } else if (left.type == VAL_DATE && right.type == VAL_NUM) {
                *result = val_date(left.date - (int32_t)right.num);
            } else if (left.type == VAL_CHAR && right.type == VAL_CHAR) {
                /* Clipper/FoxPro-style trim-concat:
                   trim trailing spaces from left, append right,
                   then append trimmed spaces to preserve total length. */
                char buf[256];
                int len;
                int left_len;
                int trimmed_spaces;
                str_copy(buf, left.str, sizeof(buf));
                left_len = strlen(buf);
                len = left_len;
                while (len > 0 && buf[len - 1] == ' ') len--;
                trimmed_spaces = left_len - len;
                buf[len] = '\0';
                {
                    int rlen = strlen(right.str);
                    int avail = (int)sizeof(buf) - 1 - len;
                    if (avail > 0) {
                        if (rlen > avail) rlen = avail;
                        memcpy(buf + len, right.str, rlen);
                        len += rlen;
                    }
                }
                {
                    int avail = (int)sizeof(buf) - 1 - len;
                    if (avail > 0 && trimmed_spaces > 0) {
                        int add_spaces = trimmed_spaces;
                        if (add_spaces > avail) add_spaces = avail;
                        memset(buf + len, ' ', add_spaces);
                        len += add_spaces;
                    }
                }
                buf[len] = '\0';
                *result = val_str(buf);
            } else {
                ctx->error = "Type mismatch in -"; return -1;
            }
            return 0;

        case OP_MUL:
            if (left.type != VAL_NUM || right.type != VAL_NUM) {
                ctx->error = "Type mismatch in * or /"; return -1;
            }
            *result = val_num(left.num * right.num);
            return 0;

        case OP_DIV:
            if (left.type != VAL_NUM || right.type != VAL_NUM) {
                ctx->error = "Type mismatch in * or /"; return -1;
            }
            if (right.num == 0.0) { ctx->error = "Division by zero"; return -1; }
            *result = val_num(left.num / right.num);
            return 0;

        case OP_POWER:
            if (left.type != VAL_NUM || right.type != VAL_NUM) {
                ctx->error = "Type mismatch in **"; return -1;
            }
            {
                double r;
                errno = 0;
                r = pow(left.num, right.num);
                if (!isfinite(r) || errno != 0) {
                    ctx->error = "Invalid exponentiation"; return -1;
                }
                *result = val_num(r);
            }
            return 0;

        /* ---- Substring ---- */
        case OP_SUBSTR:
            if (left.type != VAL_CHAR || right.type != VAL_CHAR) {
                ctx->error = "$ requires strings"; return -1;
            }
            *result = val_logic(strstr(right.str, left.str) != NULL);
            return 0;

        /* ---- Comparison ---- */
        case OP_EQ: case OP_EXACT_EQ: case OP_NE:
        case OP_LT: case OP_GT: case OP_LE: case OP_GE: {
            int cmp = 0;

            if (left.type == VAL_NUM && right.type == VAL_NUM) {
                double d = left.num - right.num;
                cmp = (d < 0) ? -1 : (d > 0) ? 1 : 0;
            } else if (left.type == VAL_CHAR && right.type == VAL_CHAR) {
                if (node->binop.op == OP_EXACT_EQ)
                    cmp = str_icmp(left.str, right.str);
                else if (ctx->opts && !((set_options_t *)ctx->opts)->exact)
                    cmp = str_nicmp(left.str, right.str, strlen(right.str));
                else
                    cmp = str_icmp(left.str, right.str);
            } else if (left.type == VAL_DATE && right.type == VAL_DATE) {
                int32_t d = left.date - right.date;
                cmp = (d < 0) ? -1 : (d > 0) ? 1 : 0;
            } else if (left.type == VAL_LOGIC && right.type == VAL_LOGIC) {
                cmp = left.logic - right.logic;
            } else {
                ctx->error = "Type mismatch in comparison"; return -1;
            }

            switch (node->binop.op) {
            case OP_EQ:       case OP_EXACT_EQ: *result = val_logic(cmp == 0); break;
            case OP_NE:       *result = val_logic(cmp != 0); break;
            case OP_LT:       *result = val_logic(cmp < 0); break;
            case OP_GT:       *result = val_logic(cmp > 0); break;
            case OP_LE:       *result = val_logic(cmp <= 0); break;
            case OP_GE:       *result = val_logic(cmp >= 0); break;
            default: break;
            }
            return 0;
        }

        /* ---- Logical ---- */
        case OP_AND:
            if (left.type != VAL_LOGIC || right.type != VAL_LOGIC) {
                ctx->error = "Type mismatch in .AND."; return -1;
            }
            *result = val_logic(left.logic && right.logic);
            return 0;

        case OP_OR:
            if (left.type != VAL_LOGIC || right.type != VAL_LOGIC) {
                ctx->error = "Type mismatch in .OR."; return -1;
            }
            *result = val_logic(left.logic || right.logic);
            return 0;

        default:
            ctx->error = "Unknown binary operator";
            return -1;
        }
    } /* end AST_BINOP */

    } /* end switch(node->type) */

    ctx->error = "Unknown AST node type";
    return -1;
}

int ast_eval_dynamic(const char *expr, expr_ctx_t *ctx, value_t *result) {
    const char *error;
    ast_node_t *ast = ast_compile(expr, ctx->vars, &error);
    if (!ast) return expr_eval_str(ctx, expr, result);
    int rc = ast_eval(ast, ctx, result);
    ast_free(ast);
    return rc;
}
