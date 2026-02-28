#ifndef LISP_EVAL_H
#define LISP_EVAL_H

#include "types.h"

val_t eval(val_t expr, val_t env);
void eval_init(void);

/* Global environment */
extern val_t global_env;

#endif
