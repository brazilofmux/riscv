#ifndef LISP_HEAP_H
#define LISP_HEAP_H

#include "types.h"

/* Root stack for GC safety */
#define ROOT_STACK_SIZE 512
extern val_t *root_stack[ROOT_STACK_SIZE];
extern int root_sp;

void push_root_checked(val_t *p);
#define PUSH_ROOT(v) push_root_checked(&(v))
#define POP_ROOTS(n) (root_sp -= (n))

void heap_init(void);
void gc_collect(void);

val_t cons_alloc(val_t car, val_t cdr);
val_t symbol_intern(const char *name);
val_t string_alloc(const char *data, int len);
val_t lambda_alloc(val_t params, val_t body, val_t env);
val_t builtin_alloc(const char *name, val_t (*fn)(val_t));

/* Symbol table access for #t */
extern val_t sym_true;

#endif
