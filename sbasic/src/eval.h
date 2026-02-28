#ifndef SBASIC_EVAL_H
#define SBASIC_EVAL_H

#include "ast.h"
#include "env.h"
#include "error.h"

/* Evaluate an expression, storing result in *out */
error_t eval_expr(env_t *env, expr_t *e, value_t *out);

/* Execute a single statement */
error_t eval_stmt(env_t *env, stmt_t *s);

/* Execute a statement chain (linked list traversal) */
error_t eval_stmts(env_t *env, stmt_t *stmts);

/* Execute a program with GOTO/GOSUB/SUB/FUNCTION support.
   Flattens the statement list to an array for indexed jumps.
   First pass collects labels and procedure definitions. */
error_t eval_program(env_t *env, stmt_t *program);

/* Current print position (column 0-based, row 1-based) */
extern int print_col;
extern int print_row;

/* Points to the active column counter (stdout or file) for TAB() */
extern int *active_print_col;

#endif
