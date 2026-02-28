#ifndef AST_H
#define AST_H

#include "expr.h"
#include "memvar.h"

/* ---- AST node types ---- */

typedef enum {
    AST_LITERAL,        /* value_t embedded */
    AST_FIELD,          /* field name — resolved at eval time */
    AST_ALIAS_FIELD,    /* alias->field */
    AST_MEMVAR,         /* bare name (field-first lookup at eval time) */
    AST_ARRAY_ACCESS,   /* arr[i] or arr[i,j] */
    AST_BINOP,          /* left OP right */
    AST_UNOP,           /* OP child (negate, .NOT.) */
    AST_FUNC_CALL       /* name(args...) */
} ast_type_t;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POWER,
    OP_EQ, OP_EXACT_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE, OP_SUBSTR,
    OP_AND, OP_OR,
    OP_NOT, OP_NEGATE
} ast_op_t;

typedef struct ast_node {
    ast_type_t type;
    union {
        value_t literal;                                        /* AST_LITERAL */
        char name[64];                                          /* AST_FIELD, AST_MEMVAR */
        struct { char alias[64]; char field[64]; } alias_field; /* AST_ALIAS_FIELD */
        struct { char name[64]; struct ast_node *index1;
                 struct ast_node *index2; } array;              /* AST_ARRAY_ACCESS */
        struct { ast_op_t op; struct ast_node *left;
                 struct ast_node *right; } binop;               /* AST_BINOP */
        struct { ast_op_t op; struct ast_node *child; } unop;   /* AST_UNOP */
        struct { char name[64]; struct ast_node **args;
                 int nargs; } call;                             /* AST_FUNC_CALL */
    };
} ast_node_t;

/* ---- API ---- */

/* Parse expression string into AST.  Returns NULL on error (sets *error).
   Does NOT evaluate — pure parsing, no side effects. */
ast_node_t *ast_compile(const char *expr, memvar_store_t *store, const char **error);

/* Same, but advances *pp past consumed input. */
ast_node_t *ast_compile_adv(const char **pp, memvar_store_t *store, const char **error);

/* Evaluate AST.  Returns 0 on success, -1 on error. */
int ast_eval(ast_node_t *node, expr_ctx_t *ctx, value_t *result);

/* Compile, evaluate, and free a fresh AST in one shot.
   Used for expressions containing &macros that must re-expand each time. */
int ast_eval_dynamic(const char *expr, expr_ctx_t *ctx, value_t *result);

/* Free AST recursively.  Safe to call with NULL. */
void ast_free(ast_node_t *node);

#endif
