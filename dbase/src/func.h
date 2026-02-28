#ifndef FUNC_H
#define FUNC_H

#include "expr.h"

/* Call a built-in function by name. Returns 0 on success, -1 on error. */
int func_call(expr_ctx_t *ctx, const char *name, value_t *args, int nargs, value_t *result);

/* User-defined function callback. Set by program.c. */
typedef int (*udf_callback_t)(const char *name, value_t *args, int nargs, value_t *result);
void func_set_udf_callback(udf_callback_t cb);

/* Cleanup low-level file handles */
void ll_close_all(void);

#endif
