#ifndef SBASIC_ARRAY_H
#define SBASIC_ARRAY_H

#include "value.h"
#include "error.h"

#define MAX_ARRAY_DIMS 8
#define MAX_ARRAYS 64

typedef struct {
    char name[64];
    val_type_t elem_type;
    int ndims;
    int dims[MAX_ARRAY_DIMS];   /* size of each dimension */
    int base;                    /* lower bound (OPTION BASE) */
    int total;                   /* total number of elements */
    value_t *data;              /* flat row-major storage */
    int active;                  /* 1 if allocated */
} sb_array_t;

/* Clear all arrays (call at program start) */
void array_clear_all(void);

/* Create an array. sizes[] are dimension sizes (not upper bounds). */
error_t array_dim(const char *name, val_type_t type,
                  int *sizes, int ndims, int base);

/* Find an array by name (NULL if not found) */
sb_array_t *array_find(const char *name);

/* Get pointer to element at indices (does NOT copy) */
error_t array_get(const char *name, int *indices, int nindices,
                  value_t **out);

/* Set element at indices */
error_t array_set(const char *name, int *indices, int nindices,
                  const value_t *val);

/* Erase a single array */
void array_erase(const char *name);

/* Redimension (preserve=1 keeps data) */
error_t array_redim(const char *name, val_type_t type,
                    int *sizes, int ndims, int base, int preserve);

#endif
