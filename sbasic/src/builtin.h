#ifndef SBASIC_BUILTIN_H
#define SBASIC_BUILTIN_H

#include "value.h"
#include "error.h"

/* Look up a built-in function by name. Returns 1 if found, 0 if not.
   If found, evaluates and stores result in *out. */
error_t builtin_call(const char *name, value_t *args, int nargs, value_t *out);

/* Check if name is a known built-in function */
int builtin_exists(const char *name);

/* Set RNG seed (for RANDOMIZE statement) */
void builtin_randomize(int seed);

#endif
