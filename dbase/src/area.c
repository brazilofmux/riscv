#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "area.h"
#include "util.h"

static work_area_t areas[MAX_AREAS];
static int current_area_idx = 0;

void area_init_all(void) {
    int i;
    memset(areas, 0, sizeof(areas));
    for (i = 0; i < MAX_AREAS; i++) {
        dbf_init(&areas[i].db);
        areas[i].relation_target = -1;
    }
    current_area_idx = 0;
}

int area_get_current_idx(void) {
    return current_area_idx;
}

void area_set_current_idx(int idx) {
    if (idx >= 0 && idx < MAX_AREAS) {
        current_area_idx = idx;
    }
}

work_area_t *area_get(int idx) {
    if (idx >= 0 && idx < MAX_AREAS) {
        return &areas[idx];
    }
    return NULL;
}

work_area_t *area_get_current(void) {
    return &areas[current_area_idx];
}

static int alias_token_len(const char *alias) {
    int len = 0;
    if (!alias) return 0;
    while (alias[len] && is_ident_char(alias[len])) len++;
    return len;
}

int area_resolve_alias(const char *alias) {
    int i;
    int len;
    char c;

    len = alias_token_len(alias);
    if (len == 0) return -1;

    /* 1. Try numeric 1-10 (token must be all digits) */
    if (alias[0] >= '0' && alias[0] <= '9') {
        int val = 0;
        for (i = 0; i < len; i++) {
            if (alias[i] < '0' || alias[i] > '9') {
                val = -1;
                break;
            }
            val = val * 10 + (alias[i] - '0');
        }
        if (val >= 1 && val <= MAX_AREAS)
            return val - 1;
    }

    /* 2. Try single letter A-J */
    if (len == 1) {
        c = alias[0];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c >= 'A' && c <= 'J')
            return c - 'A';
    }

    /* 3. Try named alias (token length must match) */
    for (i = 0; i < MAX_AREAS; i++) {
        if (areas[i].alias[0]) {
            int alen = (int)strlen(areas[i].alias);
            if (alen == len && str_nicmp(areas[i].alias, alias, len) == 0)
                return i;
        }
    }

    return -1;
}

dbf_t *area_lookup_dbf(const char *alias) {
    int idx = area_resolve_alias(alias);
    if (idx >= 0) return &areas[idx].db;
    return NULL;
}

void area_invalidate_all(const char *filename) {
    int i;
    for (i = 0; i < MAX_AREAS; i++) {
        dbf_t *db = &areas[i].db;
        if (dbf_is_open(db) && str_icmp(db->filename, filename) == 0) {
            dbf_cache_invalidate(db);
        }
    }
}
