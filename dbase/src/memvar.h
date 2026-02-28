#ifndef MEMVAR_H
#define MEMVAR_H

#include "expr.h"

#define MEMVAR_MAX     256
#define MEMVAR_NAMELEN  11

typedef struct array_s {
    int rows;
    int cols;
    value_t *elements;
} array_t;

typedef struct {
    char name[MEMVAR_NAMELEN];
    value_t val;
    int used;
    int scope_depth;    /* call_depth at which this var was created */
    int is_local;       /* 1 = LOCAL variable, only visible at exact scope */
} memvar_t;

typedef struct memvar_store {
    memvar_t vars[MEMVAR_MAX];
    int count;
    int current_depth;  /* updated on push/pop frame */
} memvar_store_t;

void memvar_init(memvar_store_t *store);
int  memvar_find(const memvar_store_t *store, const char *name, value_t *val);
int  memvar_set(memvar_store_t *store, const char *name, const value_t *val);
int  memvar_declare_array(memvar_store_t *store, const char *name, int rows, int cols);
int  memvar_set_elem(memvar_store_t *store, const char *name, int row, int col, const value_t *val);
int  memvar_get_elem(const memvar_store_t *store, const char *name, int row, int col, value_t *val);
int  memvar_set_local(memvar_store_t *store, const char *name, const value_t *val);
int  memvar_release(memvar_store_t *store, const char *name);
void memvar_release_all(memvar_store_t *store);
int  memvar_release_matching(memvar_store_t *store, const char *pattern, int like);
void memvar_display(const memvar_store_t *store);

#endif
