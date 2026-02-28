#ifndef LISP_ENV_H
#define LISP_ENV_H

#include "types.h"

val_t env_create(val_t parent);
val_t env_lookup(val_t env, val_t sym);
void env_define(val_t env, val_t sym, val_t val);
int env_set(val_t env, val_t sym, val_t val);
val_t env_extend(val_t parent, val_t params, val_t args);

#endif
