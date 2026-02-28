#include "array.h"
#include <stdlib.h>
#include <string.h>

static sb_array_t arrays[MAX_ARRAYS];
static int array_count = 0;

void array_clear_all(void) {
    for (int i = 0; i < array_count; i++) {
        if (arrays[i].active) {
            for (int j = 0; j < arrays[i].total; j++)
                val_clear(&arrays[i].data[j]);
            free(arrays[i].data);
            arrays[i].active = 0;
        }
    }
    array_count = 0;
}

sb_array_t *array_find(const char *name) {
    for (int i = 0; i < array_count; i++) {
        if (arrays[i].active && strcmp(arrays[i].name, name) == 0)
            return &arrays[i];
    }
    return NULL;
}

error_t array_dim(const char *name, val_type_t type,
                  int *sizes, int ndims, int base) {
    if (array_find(name))
        return ERR_DUPLICATE_DIM;

    /* Reuse an inactive slot before growing the table (#6 fix) */
    int slot = -1;
    for (int i = 0; i < array_count; i++) {
        if (!arrays[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        if (array_count >= MAX_ARRAYS)
            return ERR_OUT_OF_MEMORY;
        slot = array_count++;
    }

    int total = 1;
    for (int i = 0; i < ndims; i++) {
        if (sizes[i] <= 0) return ERR_ILLEGAL_FUNCTION_CALL;
        total *= sizes[i];
    }

    sb_array_t *arr = &arrays[slot];
    memset(arr, 0, sizeof(sb_array_t));
    strncpy(arr->name, name, 63);
    arr->elem_type = type;
    arr->ndims = ndims;
    for (int i = 0; i < ndims; i++)
        arr->dims[i] = sizes[i];
    arr->base = base;
    arr->total = total;
    arr->data = calloc(total, sizeof(value_t));
    if (!arr->data) return ERR_OUT_OF_MEMORY;
    arr->active = 1;

    for (int i = 0; i < total; i++)
        arr->data[i] = val_default(type);

    return ERR_NONE;
}

static int calc_flat_index(sb_array_t *arr, int *indices, int nindices) {
    if (nindices != arr->ndims) return -1;
    int flat = 0;
    int multiplier = 1;
    for (int d = nindices - 1; d >= 0; d--) {
        int idx = indices[d] - arr->base;
        if (idx < 0 || idx >= arr->dims[d]) return -1;
        flat += idx * multiplier;
        multiplier *= arr->dims[d];
    }
    return flat;
}

error_t array_get(const char *name, int *indices, int nindices,
                  value_t **out) {
    sb_array_t *arr = array_find(name);
    if (!arr) return ERR_UNDEFINED_VAR;
    int flat = calc_flat_index(arr, indices, nindices);
    if (flat < 0) return ERR_SUBSCRIPT_OUT_OF_RANGE;
    *out = &arr->data[flat];
    return ERR_NONE;
}

error_t array_set(const char *name, int *indices, int nindices,
                  const value_t *val) {
    sb_array_t *arr = array_find(name);
    if (!arr) return ERR_UNDEFINED_VAR;
    int flat = calc_flat_index(arr, indices, nindices);
    if (flat < 0) return ERR_SUBSCRIPT_OUT_OF_RANGE;
    val_assign(&arr->data[flat], val);
    return ERR_NONE;
}

void array_erase(const char *name) {
    for (int i = 0; i < array_count; i++) {
        if (arrays[i].active && strcmp(arrays[i].name, name) == 0) {
            for (int j = 0; j < arrays[i].total; j++)
                val_clear(&arrays[i].data[j]);
            free(arrays[i].data);
            arrays[i].active = 0;
            return;
        }
    }
}

error_t array_redim(const char *name, val_type_t type,
                    int *sizes, int ndims, int base, int preserve) {
    sb_array_t *arr = array_find(name);
    if (!arr)
        return array_dim(name, type, sizes, ndims, base);

    int new_total = 1;
    for (int i = 0; i < ndims; i++) {
        if (sizes[i] <= 0) return ERR_ILLEGAL_FUNCTION_CALL;
        new_total *= sizes[i];
    }

    if (preserve) {
        /* Dimension count must match for PRESERVE to be meaningful */
        if (ndims != arr->ndims)
            return ERR_ILLEGAL_FUNCTION_CALL;

        value_t *new_data = calloc(new_total, sizeof(value_t));
        if (!new_data) return ERR_OUT_OF_MEMORY;

        for (int i = 0; i < new_total; i++)
            new_data[i] = val_default(type);

        /* Coordinate-aware copy through shared bounds (#3 fix).
         * For each coordinate within min(old,new) of every dimension,
         * compute separate old/new flat indices and copy the value. */
        int shared[MAX_ARRAY_DIMS];
        for (int d = 0; d < ndims; d++)
            shared[d] = arr->dims[d] < sizes[d] ? arr->dims[d] : sizes[d];

        int coord[MAX_ARRAY_DIMS];
        memset(coord, 0, sizeof(coord));
        for (;;) {
            /* Old flat index (row-major, old dims) */
            int old_flat = 0, old_mult = 1;
            for (int d = ndims - 1; d >= 0; d--) {
                old_flat += coord[d] * old_mult;
                old_mult *= arr->dims[d];
            }
            /* New flat index (row-major, new dims) */
            int new_flat = 0, new_mult = 1;
            for (int d = ndims - 1; d >= 0; d--) {
                new_flat += coord[d] * new_mult;
                new_mult *= sizes[d];
            }
            val_clear(&new_data[new_flat]);
            new_data[new_flat] = val_copy(&arr->data[old_flat]);

            /* Increment coordinate (rightmost dimension first) */
            int d = ndims - 1;
            while (d >= 0) {
                if (++coord[d] < shared[d]) break;
                coord[d] = 0;
                d--;
            }
            if (d < 0) break;
        }

        for (int i = 0; i < arr->total; i++)
            val_clear(&arr->data[i]);
        free(arr->data);
        arr->data = new_data;
    } else {
        for (int i = 0; i < arr->total; i++)
            val_clear(&arr->data[i]);
        free(arr->data);
        arr->data = calloc(new_total, sizeof(value_t));
        if (!arr->data) return ERR_OUT_OF_MEMORY;
        for (int i = 0; i < new_total; i++)
            arr->data[i] = val_default(type);
    }

    arr->elem_type = type;
    arr->ndims = ndims;
    for (int i = 0; i < ndims; i++)
        arr->dims[i] = sizes[i];
    arr->base = base;
    arr->total = new_total;

    return ERR_NONE;
}
