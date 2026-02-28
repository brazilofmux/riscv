#ifndef AREA_H
#define AREA_H

#include "dbf.h"
#include "index.h"

struct ast_node;

#define MAX_AREAS 10

typedef struct {
    dbf_t db;
    char alias[DBF_MAX_FIELD_NAME];
    char locate_cond[256];
    uint32_t locate_last_rec;
    char filter_cond[256];
    struct ast_node *filter_ast;
    index_t indexes[MAX_INDEXES];
    int num_indexes;
    int order;
    char relation_expr[256];
    int relation_target;
} work_area_t;

void area_init_all(void);
int area_get_current_idx(void);
void area_set_current_idx(int idx);
work_area_t *area_get(int idx);
work_area_t *area_get_current(void);

/* Resolve alias (including A-J and 1-10) to area index. Returns -1 if not found. */
int area_resolve_alias(const char *alias);

/* Resolve alias to dbf pointer. Returns NULL if not found. */
dbf_t *area_lookup_dbf(const char *alias);

/* Invalidate cache for all work areas using specified filename */
void area_invalidate_all(const char *filename);

#endif
