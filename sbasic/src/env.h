#ifndef SBASIC_ENV_H
#define SBASIC_ENV_H

#include "value.h"
#include "error.h"

/* Hash table entry for variable storage */
typedef struct var_entry {
    char name[64];
    value_t value;
    int is_const;           /* 1 if CONST variable */
    value_t *link;          /* non-NULL → reads/writes go through this pointer (SHARED) */
    struct var_entry *next;
} var_entry_t;

#define ENV_HASH_SIZE 127

/* Environment (scope) */
typedef struct env {
    var_entry_t *table[ENV_HASH_SIZE];
    struct env *parent;    /* enclosing scope (NULL for global) */
} env_t;

/* Hook for DEFTYPE: if set, called to determine the default type
   for a bare variable name (no $/%/# suffix).  First letter is passed. */
extern val_type_t (*env_deftype_hook)(char first_letter);

/* Create/destroy/clear environment */
env_t *env_create(env_t *parent);
void env_destroy(env_t *env);
void env_clear(env_t *env);

/* Get variable value. Auto-creates with default if not found in any scope.
   Returns pointer to stored value (valid until next env operation). */
value_t *env_get(env_t *env, const char *name);

/* Set variable in current scope */
void env_set(env_t *env, const char *name, const value_t *val);

/* Set variable in global (root) scope */
void env_set_global(env_t *env, const char *name, const value_t *val);

/* Check if variable exists in this scope (not parent) */
int env_has_local(env_t *env, const char *name);

/* Check if variable exists in any scope (without auto-creating) */
int env_exists(env_t *env, const char *name);

/* Set variable as const (creates and marks const) */
void env_set_const(env_t *env, const char *name, const value_t *val);

/* Check if a variable is const (searches scope chain) */
int env_is_const(env_t *env, const char *name);

/* Link a local variable to an external value (for SHARED) */
void env_link(env_t *env, const char *name, value_t *target);

#endif
