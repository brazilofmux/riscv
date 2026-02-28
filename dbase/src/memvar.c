#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "memvar.h"
#include "program.h"
#include "util.h"

static void array_free(array_t *arr) {
    if (!arr) return;
    if (arr->elements) free(arr->elements);
    free(arr);
}

void memvar_init(memvar_store_t *store) {
    int i;
    for (i = 0; i < MEMVAR_MAX; i++)
        store->vars[i].used = 0;
    store->count = 0;
}

int memvar_find(const memvar_store_t *store, const char *name, value_t *val) {
    int i;
    for (i = 0; i < MEMVAR_MAX; i++) {
        if (store->vars[i].used && str_icmp(store->vars[i].name, name) == 0) {
            /* LOCAL vars only visible at exact creation scope */
            if (store->vars[i].is_local &&
                store->vars[i].scope_depth != store->current_depth)
                continue;
            *val = store->vars[i].val;
            return 0;
        }
    }
    return -1;
}

int memvar_set(memvar_store_t *store, const char *name, const value_t *val) {
    int i;
    char uname[MEMVAR_NAMELEN];

    str_copy(uname, name, MEMVAR_NAMELEN);
    str_upper(uname);

    /* Update existing */
    for (i = 0; i < MEMVAR_MAX; i++) {
        if (store->vars[i].used && str_icmp(store->vars[i].name, uname) == 0) {
            /* LOCAL vars only writable at exact creation scope */
            if (store->vars[i].is_local &&
                store->vars[i].scope_depth != store->current_depth)
                continue;
            if (store->vars[i].val.type == VAL_ARRAY)
                array_free(store->vars[i].val.array);
            store->vars[i].val = *val;
            return 0;
        }
    }

    /* Find free slot */
    for (i = 0; i < MEMVAR_MAX; i++) {
        if (!store->vars[i].used) {
            str_copy(store->vars[i].name, uname, MEMVAR_NAMELEN);
            store->vars[i].val = *val;
            store->vars[i].used = 1;
            store->vars[i].scope_depth = store->current_depth;
            store->vars[i].is_local = 0;
            store->count++;
            return 0;
        }
    }

    prog_error(ERR_TOO_MANY_VARS, "Too many memory variables");
    return -1;
}

int memvar_set_local(memvar_store_t *store, const char *name, const value_t *val) {
    int i;
    char uname[MEMVAR_NAMELEN];

    str_copy(uname, name, MEMVAR_NAMELEN);
    str_upper(uname);

    /* Find free slot */
    for (i = 0; i < MEMVAR_MAX; i++) {
        if (!store->vars[i].used) {
            str_copy(store->vars[i].name, uname, MEMVAR_NAMELEN);
            store->vars[i].val = *val;
            store->vars[i].used = 1;
            store->vars[i].scope_depth = store->current_depth;
            store->vars[i].is_local = 1;
            store->count++;
            return 0;
        }
    }

    prog_error(ERR_TOO_MANY_VARS, "Too many memory variables");
    return -1;
}

int memvar_release(memvar_store_t *store, const char *name) {
    int i;
    for (i = 0; i < MEMVAR_MAX; i++) {
        if (store->vars[i].used && str_icmp(store->vars[i].name, name) == 0) {
            if (store->vars[i].val.type == VAL_ARRAY)
                array_free(store->vars[i].val.array);
            store->vars[i].used = 0;
            store->count--;
            return 0;
        }
    }
    return -1;
}

void memvar_release_all(memvar_store_t *store) {
    int i;
    for (i = 0; i < MEMVAR_MAX; i++) {
        if (!store->vars[i].used) continue;
        /* Only release variables created at or below current scope */
        if (store->vars[i].scope_depth < store->current_depth) continue;
        if (store->vars[i].val.type == VAL_ARRAY)
            array_free(store->vars[i].val.array);
        store->vars[i].used = 0;
        store->count--;
    }
}

/* Case-insensitive wildcard match: '*' and '?' */
static int pattern_match(const char *name, const char *pat) {
    return str_like(name, pat);
}

int memvar_release_matching(memvar_store_t *store, const char *pattern, int like) {
    int i, count = 0;
    for (i = 0; i < MEMVAR_MAX; i++) {
        if (!store->vars[i].used) continue;
        /* Only release variables created at or below current scope */
        if (store->vars[i].scope_depth < store->current_depth) continue;
        int matches = pattern_match(store->vars[i].name, pattern);
        if ((like && matches) || (!like && !matches)) {
            if (store->vars[i].val.type == VAL_ARRAY)
                array_free(store->vars[i].val.array);
            store->vars[i].used = 0;
            store->count--;
            count++;
        }
    }
    return count;
}

int memvar_declare_array(memvar_store_t *store, const char *name, int rows, int cols) {
    value_t v;
    int i, size;
    array_t *arr;

    if (cols < 1) cols = 1;
    if (rows < 1 || rows > 8192 || cols > 8192) return -1;
    size = rows * cols;
    if (size > 8192) return -1;

    arr = (array_t *)calloc(1, sizeof(array_t));
    if (!arr) return -1;
    arr->rows = rows;
    arr->cols = cols;
    arr->elements = (value_t *)calloc(size, sizeof(value_t));
    if (!arr->elements) { free(arr); return -1; }

    for (i = 0; i < size; i++) arr->elements[i] = val_nil();

    v.type = VAL_ARRAY;
    v.array = arr;
    if (memvar_set(store, name, &v) < 0) {
        free(arr->elements);
        free(arr);
        return -1;
    }
    return 0;
}

int memvar_set_elem(memvar_store_t *store, const char *name, int row, int col, const value_t *val) {
    int i;
    for (i = 0; i < MEMVAR_MAX; i++) {
        if (store->vars[i].used && str_icmp(store->vars[i].name, name) == 0) {
            if (store->vars[i].val.type != VAL_ARRAY) return -1;
            array_t *arr = store->vars[i].val.array;
            /* 1-based indexing in dBase */
            int r = row - 1;
            int c = col - 1;
            if (r < 0 || r >= arr->rows || c < 0 || c >= arr->cols) return -2;
            arr->elements[r * arr->cols + c] = *val;
            return 0;
        }
    }
    return -1;
}

int memvar_get_elem(const memvar_store_t *store, const char *name, int row, int col, value_t *val) {
    int i;
    for (i = 0; i < MEMVAR_MAX; i++) {
        if (store->vars[i].used && str_icmp(store->vars[i].name, name) == 0) {
            if (store->vars[i].val.type != VAL_ARRAY) return -1;
            array_t *arr = store->vars[i].val.array;
            int r = row - 1;
            int c = col - 1;
            if (r < 0 || r >= arr->rows || c < 0 || c >= arr->cols) return -2;
            *val = arr->elements[r * arr->cols + c];
            return 0;
        }
    }
    return -1;
}

void memvar_display(const memvar_store_t *store) {
    int i;
    char buf[256];
    int shown = 0;

    for (i = 0; i < MEMVAR_MAX; i++) {
        if (!store->vars[i].used) continue;
        val_to_string(&store->vars[i].val, buf, sizeof(buf));
        switch (store->vars[i].val.type) {
        case VAL_NUM:
            printf("%-10s  N  %s\n", store->vars[i].name, buf);
            break;
        case VAL_CHAR:
            printf("%-10s  C  \"%s\"\n", store->vars[i].name, buf);
            break;
        case VAL_DATE:
            printf("%-10s  D  {%s}\n", store->vars[i].name, buf);
            break;
        case VAL_LOGIC:
            printf("%-10s  L  %s\n", store->vars[i].name, buf);
            break;
        case VAL_ARRAY: {
            array_t *arr = store->vars[i].val.array;
            if (arr->cols > 1)
                printf("%-10s  A  [%d,%d]\n", store->vars[i].name, arr->rows, arr->cols);
            else
                printf("%-10s  A  [%d]\n", store->vars[i].name, arr->rows);
            break;
        }
        default:
            printf("%-10s  U  %s\n", store->vars[i].name, buf);
            break;
        }
        shown++;
    }
    printf("%d variable(s) defined.\n", shown);
}
