#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "index.h"
#include "util.h"
#include "date.h"
#include "ast.h"

static void page_free(ndx_page_t *p);
static int page_flush(index_t *idx, int page_no);
static void hash_add(index_t *idx, ndx_page_t *p);
static void hash_remove(index_t *idx, ndx_page_t *p);
static ndx_page_t *hash_lookup(index_t *idx, int page_no);
static ndx_page_t *page_get(index_t *idx, int page_no);
static void page_put(ndx_page_t *p);

void index_clear_key_ast(index_t *idx) {
    if (idx->key_ast) {
        ast_free(idx->key_ast);
        idx->key_ast = NULL;
    }
}

int index_compile_key_ast(index_t *idx, struct memvar_store *store) {
    const char *err = NULL;
    index_clear_key_ast(idx);
    idx->key_has_macro = (strchr(idx->key_expr, '&') != NULL);
    if (idx->key_has_macro || !idx->key_expr[0])
        return 0;
    idx->key_ast = ast_compile(idx->key_expr, store, &err);
    return idx->key_ast ? 0 : -1;
}

static void encode_sortable_double(double n, char *out) {
    static const char hex[] = "0123456789ABCDEF";
    uint64_t bits;
    int i;

    /* Normalize NaN to zero for deterministic key generation. */
    if (n != n) n = 0.0;

    memcpy(&bits, &n, sizeof(bits));
    if (bits & 0x8000000000000000ULL)
        bits = ~bits;
    else
        bits ^= 0x8000000000000000ULL;

    for (i = 15; i >= 0; i--) {
        out[i] = hex[bits & 0x0F];
        bits >>= 4;
    }
    out[16] = '\0';
}

void index_format_key_value(int key_type, const value_t *val, char *buf, int size) {
    if (size <= 0) return;
    buf[0] = '\0';

    if (key_type == 1) {
        char tmp[17];
        double n = 0.0;
        if (val->type == VAL_NUM) n = val->num;
        else if (val->type == VAL_CHAR) n = atof(val->str);
        encode_sortable_double(n, tmp);
        str_copy(buf, tmp, size);
        return;
    }

    if (key_type == 2) {
        char tmp[9];
        int32_t d = 0;
        if (val->type == VAL_DATE) d = val->date;
        else if (val->type == VAL_NUM) d = (int32_t)val->num;
        else if (val->type == VAL_CHAR && strlen(val->str) == 8) d = date_from_dbf(val->str);
        date_to_dbf(d, tmp);  /* canonical YYYYMMDD for index keys */
        str_copy(buf, tmp, size);
        return;
    }

    val_to_string(val, buf, size);
}

/* ---- Key comparison ---- */
static int key_cmp(const char *a, const char *b, int key_len) {
    return memcmp(a, b, key_len);
}

static char *page_key(index_t *idx, ndx_page_t *page, int i) {
    return page->keys + i * idx->key_len;
}

/* ---- LRU cache helpers ---- */
static void cache_remove(index_t *idx, ndx_page_t *p) {
    if (!p) return;
    if (p->lru_prev) p->lru_prev->lru_next = p->lru_next;
    if (p->lru_next) p->lru_next->lru_prev = p->lru_prev;
    if (idx->lru_head == p) idx->lru_head = p->lru_next;
    if (idx->lru_tail == p) idx->lru_tail = p->lru_prev;
    p->lru_prev = NULL;
    p->lru_next = NULL;
    if (idx->cache_size > 0) idx->cache_size--;
}

static void cache_add(index_t *idx, ndx_page_t *p) {
    if (!p) return;
    p->lru_prev = NULL;
    p->lru_next = idx->lru_head;
    if (idx->lru_head) idx->lru_head->lru_prev = p;
    idx->lru_head = p;
    if (!idx->lru_tail) idx->lru_tail = p;
    idx->cache_size++;
}

static void cache_touch(index_t *idx, ndx_page_t *p) {
    if (!p || idx->lru_head == p) return;
    cache_remove(idx, p);
    cache_add(idx, p);
}

static void cache_evict(index_t *idx) {
    ndx_page_t *p;
    if (!idx->fp) return; /* don't evict if we can't flush */
    while (idx->cache_size > idx->cache_capacity && idx->lru_tail) {
        p = idx->lru_tail;
        while (p && p->pin_count > 0)
            p = p->lru_prev;
        if (!p) return; /* all pages pinned */
        if (p->dirty) page_flush(idx, p->page_no);
        cache_remove(idx, p);
        hash_remove(idx, p);
        page_free(p);
    }
}

/* ---- Page Hash Table (Sparse Page Table) ---- */
static int page_hash_idx(int page_no) {
    return (unsigned int)page_no % PAGE_HASH_SIZE;
}

static ndx_page_t *hash_lookup(index_t *idx, int page_no) {
    int h = page_hash_idx(page_no);
    ndx_page_t *p = idx->page_hash[h];
    while (p) {
        if (p->page_no == page_no) return p;
        p = p->hash_next;
    }
    return NULL;
}

static void hash_add(index_t *idx, ndx_page_t *p) {
    int h = page_hash_idx(p->page_no);
    p->hash_next = idx->page_hash[h];
    idx->page_hash[h] = p;
}

static void hash_remove(index_t *idx, ndx_page_t *p) {
    int h = page_hash_idx(p->page_no);
    ndx_page_t **curr = &idx->page_hash[h];
    while (*curr) {
        if (*curr == p) {
            *curr = p->hash_next;
            p->hash_next = NULL;
            return;
        }
        curr = &(*curr)->hash_next;
    }
}

static void page_reclaim(index_t *idx, ndx_page_t *p) {
    p->type = PAGE_FREE;
    p->nkeys = 0;
    if (p->children) {
        memset(p->children, 0, (idx->max_keys_internal + 2) * sizeof(int));
    } else {
        p->children = (int *)calloc(idx->max_keys_internal + 2, sizeof(int));
    }
    p->children[0] = idx->free_page_head;
    idx->free_page_head = p->page_no;
    p->dirty = 1;
}

/* ---- Page allocation ---- */
static ndx_page_t *page_new(index_t *idx, int type) {
    ndx_page_t *p;
    int max_keys;
    int pg_no;

    if (idx->free_page_head > 0) {
        pg_no = idx->free_page_head;
        p = page_get(idx, pg_no);
        if (p) {
            idx->free_page_head = p->children[0];
            /* Re-initialize reuse page.  Free all arrays and reallocate
               for the target type to avoid buffer size mismatches when a
               leaf page is recycled as internal (or vice versa). */
            p->type = type;
            p->nkeys = 0;
            p->dirty = 1;
            p->next_leaf = 0;
            p->prev_leaf = 0;
            free(p->keys);      p->keys = NULL;
            free(p->recnos);    p->recnos = NULL;
            free(p->children);  p->children = NULL;

            if (type == PAGE_LEAF) {
                p->keys = (char *)calloc(idx->max_keys_leaf + 1, idx->key_len);
                p->recnos = (uint32_t *)calloc(idx->max_keys_leaf + 1, sizeof(uint32_t));
                if (!p->keys || !p->recnos) { page_free(p); return NULL; }
            } else {
                p->keys = (char *)calloc(idx->max_keys_internal + 1, idx->key_len);
                p->children = (int *)calloc(idx->max_keys_internal + 2, sizeof(int));
                if (!p->keys || !p->children) { page_free(p); return NULL; }
            }
            return p;
        }
    }

    pg_no = idx->num_pages;
    idx->num_pages++;

    p = (ndx_page_t *)calloc(1, sizeof(ndx_page_t));
    if (!p) return NULL;

    p->page_no = pg_no;
    p->type = type;
    p->nkeys = 0;
    p->dirty = 1;
    p->next_leaf = 0;
    p->prev_leaf = 0;
    p->pin_count = 1;
    p->lru_prev = NULL;
    p->lru_next = NULL;

    if (type == PAGE_LEAF) {
        max_keys = idx->max_keys_leaf;
        /* +1 to allow temporary overflow before split */
        p->keys = (char *)calloc(max_keys + 1, idx->key_len);
        p->recnos = (uint32_t *)calloc(max_keys + 1, sizeof(uint32_t));
        p->children = NULL;
        if (!p->keys || !p->recnos) { page_free(p); return NULL; }
    } else {
        max_keys = idx->max_keys_internal;
        /* +1 keys and +2 children for temporary overflow before split */
        p->keys = (char *)calloc(max_keys + 1, idx->key_len);
        p->children = (int *)calloc(max_keys + 2, sizeof(int));
        p->recnos = NULL;
        if (!p->keys || !p->children) { page_free(p); return NULL; }
    }

    hash_add(idx, p);
    cache_add(idx, p);
    cache_evict(idx);
    return p;
}

static void page_free(ndx_page_t *p) {
    if (!p) return;
    if (p->keys) free(p->keys);
    if (p->recnos) free(p->recnos);
    if (p->children) free(p->children);
    free(p);
}

/* ---- Page I/O ---- */
static int page_flush(index_t *idx, int page_no) {
    unsigned char buf[NDX_PAGE_SIZE];
    ndx_page_t *p;
    int i, off;

    if (!idx->fp) return -1;
    p = hash_lookup(idx, page_no);
    if (page_no != 0 && !p) return -1;

    memset(buf, 0, NDX_PAGE_SIZE);

    if (page_no == 0) {
        /* Header page */
        uint32_t tmp;
        tmp = NDX2_MAGIC;           memcpy(buf + 0, &tmp, 4);
        tmp = 1;                    memcpy(buf + 4, &tmp, 4);  /* version */
        tmp = idx->root_page;       memcpy(buf + 8, &tmp, 4);
        tmp = idx->first_leaf;      memcpy(buf + 12, &tmp, 4);
        tmp = idx->num_pages;       memcpy(buf + 16, &tmp, 4);
        tmp = idx->nentries;        memcpy(buf + 20, &tmp, 4);
        tmp = idx->key_len;         memcpy(buf + 24, &tmp, 4);
        tmp = idx->key_type;        memcpy(buf + 28, &tmp, 4);
        tmp = idx->free_page_head;  memcpy(buf + 32, &tmp, 4);
        tmp = idx->unique;          memcpy(buf + 36, &tmp, 4);
        memcpy(buf + 40, idx->key_expr, 256);
        memcpy(buf + 296, idx->for_expr, 256);
        tmp = idx->descending;      memcpy(buf + 552, &tmp, 4);
    } else if (p->type == PAGE_FREE) {
        uint16_t t16;
        uint32_t t32;
        t16 = PAGE_FREE;        memcpy(buf + 0, &t16, 2);
        t16 = 0;                memcpy(buf + 2, &t16, 2);
        t32 = (uint32_t)p->children[0]; memcpy(buf + 4, &t32, 4);
    } else if (p->type == PAGE_INTERNAL) {
        uint16_t t16;
        t16 = PAGE_INTERNAL;    memcpy(buf + 0, &t16, 2);
        t16 = (uint16_t)p->nkeys; memcpy(buf + 2, &t16, 2);
        off = 4;
        /* Child pointers: n+1 */
        for (i = 0; i <= p->nkeys; i++) {
            uint32_t child = (uint32_t)p->children[i];
            memcpy(buf + off, &child, 4);
            off += 4;
        }
        /* Keys: n */
        for (i = 0; i < p->nkeys; i++) {
            memcpy(buf + off, page_key(idx, p, i), idx->key_len);
            off += idx->key_len;
        }
    } else {
        /* Leaf */
        uint16_t t16;
        uint32_t t32;
        t16 = PAGE_LEAF;       memcpy(buf + 0, &t16, 2);
        t16 = (uint16_t)p->nkeys; memcpy(buf + 2, &t16, 2);
        t32 = (uint32_t)p->next_leaf; memcpy(buf + 4, &t32, 4);
        t32 = (uint32_t)p->prev_leaf; memcpy(buf + 8, &t32, 4);
        off = 12;
        for (i = 0; i < p->nkeys; i++) {
            memcpy(buf + off, page_key(idx, p, i), idx->key_len);
            off += idx->key_len;
            memcpy(buf + off, &p->recnos[i], 4);
            off += 4;
        }
    }

    fseek(idx->fp, (long)page_no * NDX_PAGE_SIZE, SEEK_SET);
    if (fwrite(buf, NDX_PAGE_SIZE, 1, idx->fp) != 1)
        return -1;
    fflush(idx->fp);
    p->dirty = 0;
    return 0;
}

static ndx_page_t *page_read(index_t *idx, int page_no, unsigned char *buf) {
    ndx_page_t *p;
    uint16_t t16;
    int i, off;

    if (page_no == 0) return NULL; /* header page handled separately */

    memcpy(&t16, buf + 0, 2);

    if (t16 == PAGE_FREE) {
        uint32_t t32;
        p = (ndx_page_t *)calloc(1, sizeof(ndx_page_t));
        p->page_no = page_no;
        p->type = PAGE_FREE;
        p->nkeys = 0;
        p->keys = NULL;
        p->recnos = NULL;
        p->children = (int *)calloc(idx->max_keys_internal + 2, sizeof(int));
        memcpy(&t32, buf + 4, 4);
        p->children[0] = (int)t32;
        p->dirty = 0;
    } else if (t16 == PAGE_INTERNAL) {
        int max_keys = idx->max_keys_internal;
        p = (ndx_page_t *)calloc(1, sizeof(ndx_page_t));
        p->page_no = page_no;
        p->type = PAGE_INTERNAL;
        memcpy(&t16, buf + 2, 2);
        p->nkeys = t16;
        /* +1 keys and +2 children for temporary overflow before split */
        p->keys = (char *)calloc(max_keys + 1, idx->key_len);
        p->children = (int *)calloc(max_keys + 2, sizeof(int));
        p->recnos = NULL;
        p->dirty = 0;

        off = 4;
        for (i = 0; i <= p->nkeys; i++) {
            uint32_t child;
            memcpy(&child, buf + off, 4);
            p->children[i] = (int)child;
            off += 4;
        }
        for (i = 0; i < p->nkeys; i++) {
            memcpy(page_key(idx, p, i), buf + off, idx->key_len);
            off += idx->key_len;
        }
    } else {
        /* Leaf */
        int max_keys = idx->max_keys_leaf;
        uint32_t t32;
        p = (ndx_page_t *)calloc(1, sizeof(ndx_page_t));
        p->page_no = page_no;
        p->type = PAGE_LEAF;
        memcpy(&t16, buf + 2, 2);
        p->nkeys = t16;
        memcpy(&t32, buf + 4, 4); p->next_leaf = (int)t32;
        memcpy(&t32, buf + 8, 4); p->prev_leaf = (int)t32;
        /* +1 to allow temporary overflow before split */
        p->keys = (char *)calloc(max_keys + 1, idx->key_len);
        p->recnos = (uint32_t *)calloc(max_keys + 1, sizeof(uint32_t));
        p->children = NULL;
        p->dirty = 0;

        off = 12;
        for (i = 0; i < p->nkeys; i++) {
            memcpy(page_key(idx, p, i), buf + off, idx->key_len);
            off += idx->key_len;
            memcpy(&p->recnos[i], buf + off, 4);
            off += 4;
        }
    }

    return p;
}

static ndx_page_t *page_get(index_t *idx, int page_no) {
    ndx_page_t *p;
    unsigned char buf[NDX_PAGE_SIZE];

    if (page_no < 0 || page_no >= idx->num_pages) return NULL;
    p = hash_lookup(idx, page_no);
    if (p) {
        p->pin_count++;
        cache_touch(idx, p);
        return p;
    }

    if (!idx->fp) return NULL;

    fseek(idx->fp, (long)page_no * NDX_PAGE_SIZE, SEEK_SET);
    if (fread(buf, NDX_PAGE_SIZE, 1, idx->fp) != 1)
        return NULL;

    p = page_read(idx, page_no, buf);
    if (!p) return NULL;
    p->pin_count = 1;
    hash_add(idx, p);
    cache_add(idx, p);
    cache_evict(idx);
    return p;
}

static void page_put(ndx_page_t *p) {
    if (!p) return;
    if (p->pin_count > 0) p->pin_count--;
}

/* ---- Free all tree pages ---- */
static void free_all_pages(index_t *idx) {
    int i;
    for (i = 0; i < PAGE_HASH_SIZE; i++) {
        ndx_page_t *p = idx->page_hash[i];
        while (p) {
            ndx_page_t *next = p->hash_next;
            page_free(p);
            p = next;
        }
        idx->page_hash[i] = NULL;
    }
    idx->num_pages = 0;
    idx->cache_size = 0;
    idx->lru_head = NULL;
    idx->lru_tail = NULL;
}

/* ---- Init ---- */
void index_init(index_t *idx) {
    memset(idx, 0, sizeof(index_t));
    idx->iter_page = -1;
    idx->iter_pos = -1;
    idx->cache_capacity = 64;
    idx->key_ast = NULL;
    idx->key_has_macro = 0;
    idx->for_expr[0] = '\0';
    idx->for_ast = NULL;
    idx->descending = 0;
}

/* ---- Close ---- */
void index_close(index_t *idx) {
    free_all_pages(idx);
    index_clear_key_ast(idx);
    if (idx->for_ast) { ast_free(idx->for_ast); idx->for_ast = NULL; }
    if (idx->fp) {
        fclose(idx->fp);
        idx->fp = NULL;
    }
    idx->nentries = 0;
    idx->root_page = 0;
    idx->first_leaf = 0;
    idx->iter_page = -1;
    idx->iter_pos = -1;
    idx->active = 0;
    idx->filename[0] = '\0';
    idx->key_expr[0] = '\0';
    idx->key_has_macro = 0;
    idx->for_expr[0] = '\0';
    idx->descending = 0;
}

/* ---- Compute max keys from key_len ---- */
static void compute_fanout(index_t *idx) {
    /* Internal: header(4) + (n+1)*4 child ptrs + n*key_len <= 4096
       4 + 4*(n+1) + n*key_len <= 4096
       n*(4+key_len) <= 4096 - 8
       n <= (4088) / (4 + key_len) */
    idx->max_keys_internal = (NDX_PAGE_SIZE - 8) / (4 + idx->key_len);
    /* Leaf: header(12) + n*(key_len+4) <= 4096
       n <= (4084) / (key_len + 4) */
    idx->max_keys_leaf = (NDX_PAGE_SIZE - 12) / (idx->key_len + 4);

    if (idx->max_keys_internal < 3) idx->max_keys_internal = 3;
    if (idx->max_keys_leaf < 3) idx->max_keys_leaf = 3;
}

/* ---- Binary search in a page ---- */
/* Returns index of first key >= search_key (for leaves: where to insert) */
static int page_lower_bound(index_t *idx, ndx_page_t *page, const char *key) {
    int lo = 0, hi = page->nkeys;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (key_cmp(page_key(idx, page, mid), key, idx->key_len) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* ---- Binary search: upper bound ---- */
/* Returns index of first key > search_key (for internal routing) */
static int page_upper_bound(index_t *idx, ndx_page_t *page, const char *key) {
    int lo = 0, hi = page->nkeys;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (key_cmp(page_key(idx, page, mid), key, idx->key_len) <= 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* ---- Search: find leaf and position for key ---- */
static ndx_page_t *bt_find_leaf(index_t *idx, const char *key) {
    ndx_page_t *node;
    if (idx->num_pages <= 1) return NULL;
    node = page_get(idx, idx->root_page);
    while (node && node->type == PAGE_INTERNAL) {
        int pos = page_upper_bound(idx, node, key);
        int child_no = node->children[pos];
        page_put(node);
        /* descend to children[pos] */
        node = page_get(idx, child_no);
    }
    return node; /* pinned */
}

/* ---- Key existence check (for UNIQUE validation) ---- */
int index_key_exists(index_t *idx, const char *key, uint32_t exclude_recno) {
    char padded[MAX_INDEX_KEY];
    ndx_page_t *leaf;
    int pos;

    if (!idx->active || idx->nentries == 0) return 0;

    memset(padded, ' ', idx->key_len);
    {
        int len = strlen(key);
        if (len > idx->key_len) len = idx->key_len;
        memcpy(padded, key, len);
    }

    leaf = bt_find_leaf(idx, padded);
    if (!leaf) return 0;

    pos = page_lower_bound(idx, leaf, padded);
    while (pos < leaf->nkeys &&
           key_cmp(page_key(idx, leaf, pos), padded, idx->key_len) == 0) {
        if (leaf->recnos[pos] != exclude_recno) {
            page_put(leaf);
            return 1; /* duplicate exists */
        }
        pos++;
    }
    page_put(leaf);
    return 0;
}

/* ---- Seek ---- */
int index_seek(index_t *idx, const char *key, int match_len) {
    ndx_page_t *leaf;
    char padded[MAX_INDEX_KEY];
    int pos;
    int cmp_len;

    if (!idx->active || idx->nentries == 0) {
        idx->iter_page = -1;
        idx->iter_pos = -1;
        return 0;
    }

    /* Pad key to key_len */
    memset(padded, ' ', idx->key_len);
    {
        int len = strlen(key);
        if (len > idx->key_len) len = idx->key_len;
        memcpy(padded, key, len);
    }

    /* For match comparison: use match_len if specified, else full key_len */
    cmp_len = (match_len > 0 && match_len < idx->key_len) ? match_len : idx->key_len;

    leaf = bt_find_leaf(idx, padded);
    if (!leaf) {
        idx->iter_page = -1;
        idx->iter_pos = -1;
        return 0;
    }

    pos = page_lower_bound(idx, leaf, padded);

    if (pos < leaf->nkeys) {
        idx->iter_page = leaf->page_no;
        idx->iter_pos = pos;
        if (key_cmp(page_key(idx, leaf, pos), padded, cmp_len) == 0) {
            page_put(leaf);
            return 1; /* match */
        }
        page_put(leaf);
        return 0;
    }

    /* Past end of this leaf -- try next leaf */
    if (leaf->next_leaf) {
        ndx_page_t *next = page_get(idx, leaf->next_leaf);
        idx->iter_page = leaf->next_leaf;
        idx->iter_pos = 0;
        if (next && next->nkeys > 0 &&
            key_cmp(page_key(idx, next, 0), padded, cmp_len) == 0) {
            page_put(next);
            page_put(leaf);
            return 1; /* match at boundary */
        }
        page_put(next);
    } else {
        /* Past end of entire index */
        idx->iter_page = leaf->page_no;
        idx->iter_pos = leaf->nkeys;
    }
    page_put(leaf);
    return 0;
}

/* ---- Current record number ---- */
uint32_t index_current_recno(const index_t *idx) {
    ndx_page_t *page;
    if (idx->iter_page < 0 || idx->iter_page >= idx->num_pages)
        return 0;
    page = page_get((index_t *)idx, idx->iter_page);
    if (!page || page->type != PAGE_LEAF) {
        if (page) page_put(page);
        return 0;
    }
    if (idx->iter_pos < 0 || idx->iter_pos >= page->nkeys)
    {
        page_put(page);
        return 0;
    }
    {
        uint32_t rec = page->recnos[idx->iter_pos];
        page_put(page);
        return rec;
    }
}

/* ---- Navigation (ascending-order primitives) ---- */
static void index_top_asc(index_t *idx) {
    if (!idx->active || idx->nentries == 0 || idx->first_leaf == 0) {
        idx->iter_page = -1;
        idx->iter_pos = -1;
        return;
    }
    idx->iter_page = idx->first_leaf;
    idx->iter_pos = 0;
}

static void index_bottom_asc(index_t *idx) {
    int pg;
    if (!idx->active || idx->nentries == 0 || idx->first_leaf == 0) {
        idx->iter_page = -1;
        idx->iter_pos = -1;
        return;
    }
    /* Follow leaf chain to last leaf */
    pg = idx->first_leaf;
    for (;;) {
        ndx_page_t *page = page_get(idx, pg);
        if (!page) break;
        if (!page->next_leaf) {
            idx->iter_page = pg;
            idx->iter_pos = page->nkeys - 1;
            page_put(page);
            return;
        }
        pg = page->next_leaf;
        page_put(page);
    }
}

static int index_next_asc(index_t *idx) {
    ndx_page_t *page;
    if (idx->iter_page < 0 || idx->iter_page >= idx->num_pages)
        return -1;
    page = page_get(idx, idx->iter_page);
    if (!page || page->type != PAGE_LEAF) {
        if (page) page_put(page);
        return -1;
    }

    idx->iter_pos++;
    if (idx->iter_pos >= page->nkeys) {
        if (!page->next_leaf) {
            page_put(page);
            return -1; /* EOF */
        }
        idx->iter_page = page->next_leaf;
        idx->iter_pos = 0;
    }
    page_put(page);
    return 0;
}

static int index_prev_asc(index_t *idx) {
    ndx_page_t *page;
    if (idx->iter_page < 0 || idx->iter_page >= idx->num_pages)
        return -1;
    page = page_get(idx, idx->iter_page);
    if (!page || page->type != PAGE_LEAF) {
        if (page) page_put(page);
        return -1;
    }

    idx->iter_pos--;
    if (idx->iter_pos < 0) {
        if (!page->prev_leaf) {
            page_put(page);
            return -1; /* BOF */
        }
        idx->iter_page = page->prev_leaf;
        {
            ndx_page_t *prev = page_get(idx, idx->iter_page);
            if (!prev) {
                page_put(page);
                return -1;
            }
            idx->iter_pos = prev->nkeys - 1;
            page_put(prev);
        }
    }
    page_put(page);
    return 0;
}

/* ---- Public navigation: descending flag reverses direction ---- */
void index_top(index_t *idx) {
    if (idx->descending) index_bottom_asc(idx);
    else                 index_top_asc(idx);
}

void index_bottom(index_t *idx) {
    if (idx->descending) index_top_asc(idx);
    else                 index_bottom_asc(idx);
}

int index_next(index_t *idx) {
    if (idx->descending) return index_prev_asc(idx);
    else                 return index_next_asc(idx);
}

int index_prev(index_t *idx) {
    if (idx->descending) return index_next_asc(idx);
    else                 return index_prev_asc(idx);
}

/* ---- Leaf insert at position (assumes not full) ---- */
static void leaf_insert_at(index_t *idx, ndx_page_t *leaf, int pos,
                           const char *key, uint32_t recno) {
    int i;
    /* Shift right */
    for (i = leaf->nkeys; i > pos; i--) {
        memcpy(page_key(idx, leaf, i), page_key(idx, leaf, i - 1), idx->key_len);
        leaf->recnos[i] = leaf->recnos[i - 1];
    }
    memcpy(page_key(idx, leaf, pos), key, idx->key_len);
    leaf->recnos[pos] = recno;
    leaf->nkeys++;
    leaf->dirty = 1;
}

/* ---- Split a leaf page ---- */
/* Splits 'leaf' in two. The right half goes into a new page.
   Returns the new page. Sets *split_key to the first key of the new page. */
static ndx_page_t *leaf_split(index_t *idx, ndx_page_t *leaf, char *split_key) {
    ndx_page_t *right;
    int mid, i;

    mid = leaf->nkeys / 2;
    right = page_new(idx, PAGE_LEAF);
    if (!right) return NULL;

    /* Copy right half to new page */
    for (i = mid; i < leaf->nkeys; i++) {
        int j = i - mid;
        memcpy(page_key(idx, right, j), page_key(idx, leaf, i), idx->key_len);
        right->recnos[j] = leaf->recnos[i];
    }
    right->nkeys = leaf->nkeys - mid;
    leaf->nkeys = mid;

    /* Update leaf chain */
    right->next_leaf = leaf->next_leaf;
    right->prev_leaf = leaf->page_no;
    leaf->next_leaf = right->page_no;
    if (right->next_leaf) {
        ndx_page_t *next = page_get(idx, right->next_leaf);
        if (next) {
            next->prev_leaf = right->page_no;
            next->dirty = 1;
            page_put(next);
        }
    }

    leaf->dirty = 1;
    right->dirty = 1;

    /* Split key = first key of right page */
    memcpy(split_key, page_key(idx, right, 0), idx->key_len);

    return right;
}

/* ---- Internal node: insert key and right child at position ---- */
static void internal_insert_at(index_t *idx, ndx_page_t *node, int pos,
                                const char *key, int right_child) {
    int i;
    /* Shift keys and children right */
    for (i = node->nkeys; i > pos; i--) {
        memcpy(page_key(idx, node, i), page_key(idx, node, i - 1), idx->key_len);
        node->children[i + 1] = node->children[i];
    }
    memcpy(page_key(idx, node, pos), key, idx->key_len);
    node->children[pos + 1] = right_child;
    node->nkeys++;
    node->dirty = 1;
}

/* ---- Split an internal node ---- */
static ndx_page_t *internal_split(index_t *idx, ndx_page_t *node, char *promote_key) {
    ndx_page_t *right;
    int mid, i;

    mid = node->nkeys / 2;
    right = page_new(idx, PAGE_INTERNAL);
    if (!right) return NULL;

    /* The middle key gets promoted to parent */
    memcpy(promote_key, page_key(idx, node, mid), idx->key_len);

    /* Copy right half (after mid) to new node */
    for (i = mid + 1; i < node->nkeys; i++) {
        int j = i - mid - 1;
        memcpy(page_key(idx, right, j), page_key(idx, node, i), idx->key_len);
    }
    /* Copy children from mid+1 onwards */
    for (i = mid + 1; i <= node->nkeys; i++) {
        right->children[i - mid - 1] = node->children[i];
    }
    right->nkeys = node->nkeys - mid - 1;
    node->nkeys = mid;

    node->dirty = 1;
    right->dirty = 1;

    return right;
}

/* ---- Insert ---- */
int index_insert(index_t *idx, const char *key, uint32_t recno) {
    char padded[MAX_INDEX_KEY];
    ndx_page_t *node;
    int path[64];      /* stack of page numbers on descent path */
    int path_pos[64];  /* child index taken at each level */
    int depth = 0;
    int pos;
    char split_key[MAX_INDEX_KEY];

    if (!idx->active) return -1;

    /* Pad key */
    memset(padded, ' ', idx->key_len);
    {
        int len = strlen(key);
        if (len > idx->key_len) len = idx->key_len;
        memcpy(padded, key, len);
    }

    /* Handle empty tree */
    if (idx->num_pages <= 1) {
        /* Create root leaf (page 1, since page 0 is header) */
        ndx_page_t *leaf = page_new(idx, PAGE_LEAF);
        if (!leaf) return -1;
        leaf_insert_at(idx, leaf, 0, padded, recno);
        idx->root_page = leaf->page_no;
        idx->first_leaf = leaf->page_no;
        idx->nentries = 1;
        page_put(leaf);
        return 0;
    }

    /* Walk down tree to find leaf */
    node = page_get(idx, idx->root_page);
    while (node->type == PAGE_INTERNAL) {
        pos = page_upper_bound(idx, node, padded);
        path[depth] = node->page_no;
        path_pos[depth] = pos;
        depth++;
        {
            int child_no = node->children[pos];
            page_put(node);
            node = page_get(idx, child_no);
        }
    }

    /* node is now a leaf. Insert into it. */
    pos = page_lower_bound(idx, node, padded);
    if (idx->unique) {
        if (pos < node->nkeys &&
            key_cmp(page_key(idx, node, pos), padded, idx->key_len) == 0) {
            /* Duplicate key in unique index - reject */
            page_put(node);
            return 1;
        }
        if (pos == 0 && node->prev_leaf) {
            ndx_page_t *prev = page_get(idx, node->prev_leaf);
            if (prev && prev->nkeys > 0 &&
                key_cmp(page_key(idx, prev, prev->nkeys - 1), padded, idx->key_len) == 0) {
                page_put(prev);
                page_put(node);
                return 0;
            }
            if (prev) page_put(prev);
        }
    }
    leaf_insert_at(idx, node, pos, padded, recno);
    idx->nentries++;

    /* If leaf is over capacity, split upward */
    while (node->nkeys > (node->type == PAGE_LEAF ? idx->max_keys_leaf : idx->max_keys_internal)) {
        ndx_page_t *right;

        if (node->type == PAGE_LEAF) {
            right = leaf_split(idx, node, split_key);
        } else {
            right = internal_split(idx, node, split_key);
        }
        if (!right) return -1;

        if (depth == 0) {
            /* node is root: create new root */
            ndx_page_t *new_root = page_new(idx, PAGE_INTERNAL);
            if (!new_root) return -1;
            new_root->children[0] = node->page_no;
            internal_insert_at(idx, new_root, 0, split_key, right->page_no);
            idx->root_page = new_root->page_no;
            page_put(new_root);
            page_put(right);
            page_put(node);
            node = NULL;
            break;
        }

        /* Insert split_key into parent */
        depth--;
        {
            ndx_page_t *parent = page_get(idx, path[depth]);
            page_put(node);
            node = parent;
        }
        internal_insert_at(idx, node, path_pos[depth], split_key, right->page_no);
        page_put(right);
        /* Loop continues if parent also overflows */
    }

    /* Update first_leaf if needed */
    {
        ndx_page_t *fl = (idx->first_leaf > 0) ? page_get(idx, idx->first_leaf) : NULL;
        if (fl && fl->prev_leaf) {
            /* first_leaf is wrong; walk back */
            while (fl->prev_leaf) {
                int prev_no = fl->prev_leaf;
                page_put(fl);
                fl = page_get(idx, prev_no);
                if (!fl) break;
            }
            if (fl) idx->first_leaf = fl->page_no;
        }
        page_put(fl);
    }

    if (node) page_put(node);
    return 0;
}

/* ---- Remove ---- */

/* Remove entry from leaf at position pos */
static void leaf_remove_at(index_t *idx, ndx_page_t *leaf, int pos) {
    int i;
    for (i = pos; i < leaf->nkeys - 1; i++) {
        memcpy(page_key(idx, leaf, i), page_key(idx, leaf, i + 1), idx->key_len);
        leaf->recnos[i] = leaf->recnos[i + 1];
    }
    leaf->nkeys--;
    leaf->dirty = 1;
}

static void internal_remove_at(index_t *idx, ndx_page_t *node, int pos) {
    int i;
    for (i = pos; i < node->nkeys - 1; i++) {
        memcpy(page_key(idx, node, i), page_key(idx, node, i + 1), idx->key_len);
        node->children[i + 1] = node->children[i + 2];
    }
    node->nkeys--;
    node->dirty = 1;
}

static int bt_remove(index_t *idx, int page_no, const char *key, uint32_t recno) {
    ndx_page_t *node = page_get(idx, page_no);
    if (!node) return 0;

    if (node->type == PAGE_INTERNAL) {
        int pos = page_upper_bound(idx, node, key);
        int removed = bt_remove(idx, node->children[pos], key, recno);
        if (!removed) {
            page_put(node);
            return 0;
        }
        
        /* Check for underflow in the child we just returned from */
        ndx_page_t *child = page_get(idx, node->children[pos]);
        int min_keys = (child->type == PAGE_LEAF) ? ((idx->max_keys_leaf + 1) / 2) : ((idx->max_keys_internal + 1) / 2);
        
        if (child->nkeys < min_keys && child->page_no != idx->root_page) {
            /* Try to borrow from siblings */
            ndx_page_t *left = NULL, *right = NULL;
            if (pos > 0) left = page_get(idx, node->children[pos-1]);
            if (pos < node->nkeys) right = page_get(idx, node->children[pos+1]);

            if (left && left->nkeys > min_keys) {
                /* Borrow from left */
                if (child->type == PAGE_LEAF) {
                    leaf_insert_at(idx, child, 0, page_key(idx, left, left->nkeys - 1), left->recnos[left->nkeys - 1]);
                    left->nkeys--;
                    left->dirty = 1;
                    /* Update parent separator */
                    memcpy(page_key(idx, node, pos - 1), page_key(idx, child, 0), idx->key_len);
                    node->dirty = 1;
                } else {
                    /* Internal borrow: rotate through parent */
                    for (int i = child->nkeys; i > 0; i--) {
                        memcpy(page_key(idx, child, i), page_key(idx, child, i - 1), idx->key_len);
                        child->children[i + 1] = child->children[i];
                    }
                    child->children[1] = child->children[0];
                    memcpy(page_key(idx, child, 0), page_key(idx, node, pos - 1), idx->key_len);
                    child->children[0] = left->children[left->nkeys];
                    child->nkeys++;
                    child->dirty = 1;
                    memcpy(page_key(idx, node, pos - 1), page_key(idx, left, left->nkeys - 1), idx->key_len);
                    left->nkeys--;
                    left->dirty = 1;
                    node->dirty = 1;
                }
            } else if (right && right->nkeys > min_keys) {
                /* Borrow from right */
                if (child->type == PAGE_LEAF) {
                    leaf_insert_at(idx, child, child->nkeys, page_key(idx, right, 0), right->recnos[0]);
                    leaf_remove_at(idx, right, 0);
                    /* Update parent separator */
                    memcpy(page_key(idx, node, pos), page_key(idx, right, 0), idx->key_len);
                    node->dirty = 1;
                } else {
                    /* Internal borrow: rotate through parent */
                    memcpy(page_key(idx, child, child->nkeys), page_key(idx, node, pos), idx->key_len);
                    child->children[child->nkeys + 1] = right->children[0];
                    child->nkeys++;
                    child->dirty = 1;
                    memcpy(page_key(idx, node, pos), page_key(idx, right, 0), idx->key_len);
                    right->children[0] = right->children[1];
                    internal_remove_at(idx, right, 0);
                    node->dirty = 1;
                }
            } else {
                /* Must merge */
                if (left) {
                    /* Merge child into left */
                    if (child->type == PAGE_LEAF) {
                        for (int i = 0; i < child->nkeys; i++) {
                            leaf_insert_at(idx, left, left->nkeys, page_key(idx, child, i), child->recnos[i]);
                        }
                        if (idx->first_leaf == child->page_no) idx->first_leaf = left->page_no;
                        left->next_leaf = child->next_leaf;
                        if (child->next_leaf) {
                            ndx_page_t *next = page_get(idx, child->next_leaf);
                            if (next) { next->prev_leaf = left->page_no; next->dirty = 1; page_put(next); }
                        }
                    } else {
                        /* Internal merge: pull down parent separator */
                        memcpy(page_key(idx, left, left->nkeys), page_key(idx, node, pos - 1), idx->key_len);
                        left->children[left->nkeys + 1] = child->children[0];
                        left->nkeys++;
                        for (int i = 0; i < child->nkeys; i++) {
                            memcpy(page_key(idx, left, left->nkeys), page_key(idx, child, i), idx->key_len);
                            left->children[left->nkeys + 1] = child->children[i + 1];
                            left->nkeys++;
                        }
                    }
                    left->dirty = 1;
                    internal_remove_at(idx, node, pos - 1);
                    page_reclaim(idx, child);
                } else if (right) {
                    /* Merge right into child */
                    if (child->type == PAGE_LEAF) {
                        for (int i = 0; i < right->nkeys; i++) {
                            leaf_insert_at(idx, child, child->nkeys, page_key(idx, right, i), right->recnos[i]);
                        }
                        if (idx->first_leaf == right->page_no) idx->first_leaf = child->page_no;
                        child->next_leaf = right->next_leaf;
                        if (right->next_leaf) {
                            ndx_page_t *next = page_get(idx, right->next_leaf);
                            if (next) { next->prev_leaf = child->page_no; next->dirty = 1; page_put(next); }
                        }
                    } else {
                        /* Internal merge: pull down parent separator */
                        memcpy(page_key(idx, child, child->nkeys), page_key(idx, node, pos), idx->key_len);
                        child->children[child->nkeys + 1] = right->children[0];
                        child->nkeys++;
                        for (int i = 0; i < right->nkeys; i++) {
                            memcpy(page_key(idx, child, child->nkeys), page_key(idx, right, i), idx->key_len);
                            child->children[child->nkeys + 1] = right->children[i + 1];
                            child->nkeys++;
                        }
                    }
                    child->dirty = 1;
                    internal_remove_at(idx, node, pos);
                    page_reclaim(idx, right);
                }
            }
            if (left) page_put(left);
            if (right) page_put(right);
        }
        page_put(child);
        page_put(node);
        return 1;
    } else {
        /* Leaf: find exact key+recno and remove */
        int found = -1;
        for (int i = 0; i < node->nkeys; i++) {
            if (key_cmp(page_key(idx, node, i), key, idx->key_len) == 0 && node->recnos[i] == recno) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            leaf_remove_at(idx, node, found);
            idx->nentries--;
            page_put(node);
            return 1;
        }
    }
    page_put(node);
    return 0;
}

int index_remove(index_t *idx, const char *key, uint32_t recno) {
    char padded[MAX_INDEX_KEY];
    int removed;

    if (!idx->active || idx->nentries == 0) return -1;

    memset(padded, ' ', idx->key_len);
    {
        int len = strlen(key);
        if (len > idx->key_len) len = idx->key_len;
        memcpy(padded, key, len);
    }

    removed = bt_remove(idx, idx->root_page, padded, recno);
    if (!removed) return -1;

    /* Check if root needs to be collapsed */
    ndx_page_t *root = page_get(idx, idx->root_page);
    if (root && root->type == PAGE_INTERNAL && root->nkeys == 0) {
        int new_root = root->children[0];
        page_reclaim(idx, root);
        idx->root_page = new_root;
        /* Also update first_leaf if we collapsed to nothing */
        if (idx->nentries == 0) idx->first_leaf = 0;
    }
    if (root) page_put(root);

    if (idx->nentries == 0 && idx->root_page != 0) {
        /* Tree is entirely empty, but might have a single leaf left as root */
        ndx_page_t *r = page_get(idx, idx->root_page);
        if (r && r->nkeys == 0) {
            /* Keep it as empty leaf root or just let it be. 
               Standard dBase behavior is often to keep an empty root page. */
        }
        if (r) page_put(r);
    }

    return 0;
}

/* ---- Clear (for ZAP) ---- */
void index_clear(index_t *idx) {
    free_all_pages(idx);

    /* Reinitialize with header placeholder */
    {
        ndx_page_t *hdr = (ndx_page_t *)calloc(1, sizeof(ndx_page_t));
        hdr->page_no = 0;
        hdr->pin_count = 1;
        hash_add(idx, hdr);
    }
    idx->num_pages = 1;
    idx->root_page = 0;
    idx->first_leaf = 0;
    idx->nentries = 0;
    idx->iter_page = -1;
    idx->iter_pos = -1;
}

typedef struct {
    char key[MAX_INDEX_KEY];
    uint32_t recno;
} key_entry_t;

static int key_entry_cmp(const void *a, const void *b, void *userdata) {
    index_t *idx = (index_t *)userdata;
    const key_entry_t *ka = (const key_entry_t *)a;
    const key_entry_t *kb = (const key_entry_t *)b;
    int cmp = key_cmp(ka->key, kb->key, idx->key_len);
    return cmp;
}

/* ---- Determine key type/length from DBF field definitions (no records needed) ---- */
static void index_probe_key_from_schema(index_t *idx, dbf_t *db, const char *key_expr) {
    /* Simple field reference? */
    int fi = dbf_find_field(db, key_expr);
    if (fi >= 0) {
        switch (db->fields[fi].type) {
        case 'N': case 'F':
            idx->key_type = 1;
            idx->key_len = 16;
            break;
        case 'D':
            idx->key_type = 2;
            idx->key_len = 8;
            break;
        default:
            idx->key_type = 0;
            idx->key_len = db->fields[fi].length;
            break;
        }
        return;
    }

    /* Compound expression — sum field widths from '+' separated parts */
    {
        char buf[256];
        const char *p;
        int total_len = 0;
        int all_fields = 1;

        str_copy(buf, key_expr, sizeof(buf));
        p = buf;
        while (*p) {
            char part[64];
            const char *end = strchr(p, '+');
            int plen = end ? (int)(end - p) : (int)strlen(p);
            int i, start;

            /* Trim whitespace */
            if (plen >= (int)sizeof(part)) plen = (int)sizeof(part) - 1;
            memcpy(part, p, plen);
            part[plen] = '\0';
            start = 0;
            while (part[start] == ' ') start++;
            i = plen - 1;
            while (i > start && part[i] == ' ') part[i--] = '\0';

            fi = dbf_find_field(db, part + start);
            if (fi >= 0) {
                total_len += db->fields[fi].length;
            } else {
                all_fields = 0;
                break;
            }
            p = end ? end + 1 : p + plen;
        }

        if (all_fields && total_len > 0) {
            idx->key_type = 0;
            if (total_len > MAX_INDEX_KEY) total_len = MAX_INDEX_KEY;
            idx->key_len = total_len;
            return;
        }
    }

    /* Fallback: expression with function calls, etc. — use a reasonable default */
    idx->key_type = 0;
    idx->key_len = 10;
}

/* ---- Probe key type and length without storing entries (O(1) memory) ---- */
static void index_probe_key_info(index_t *idx, dbf_t *db, expr_ctx_t *ctx,
                                  const char *key_expr) {
    uint32_t i;
    int max_len = 0;
    int key_type_set = 0;

    idx->key_len = 10;
    idx->key_type = 0;

    for (i = 1; i <= db->record_count; i++) {
        value_t val;
        char keybuf[MAX_INDEX_KEY + 1];
        int len;

        dbf_read_record(db, i);
        if (db->record_buf[0] == '*') continue;

        if (idx->for_ast) {
            value_t fcond;
            if (ast_eval(idx->for_ast, ctx, &fcond) != 0) continue;
            if (fcond.type == VAL_LOGIC && !fcond.logic) continue;
            if (fcond.type == VAL_NUM && fcond.num == 0.0) continue;
        }

        if (idx->key_ast) {
            if (ast_eval(idx->key_ast, ctx, &val) != 0) continue;
        } else {
            if (expr_eval_str(ctx, key_expr, &val) != 0) continue;
        }

        if (!key_type_set) {
            if (val.type == VAL_NUM) idx->key_type = 1;
            else if (val.type == VAL_DATE) idx->key_type = 2;
            else idx->key_type = 0;
            key_type_set = 1;
        }

        /* Numeric and date have fixed key lengths — no need to scan further */
        if (idx->key_type == 1 || idx->key_type == 2) break;

        index_format_key_value(idx->key_type, &val, keybuf, sizeof(keybuf));
        len = strlen(keybuf);
        if (len > max_len) max_len = len;
    }

    if (!key_type_set) {
        /* No records — fall back to schema-based probe */
        index_probe_key_from_schema(idx, db, key_expr);
    } else if (idx->key_type == 1) {
        idx->key_len = 16;
    } else if (idx->key_type == 2) {
        idx->key_len = 8;
    } else {
        if (max_len < 1) max_len = 1;
        if (max_len > MAX_INDEX_KEY) max_len = MAX_INDEX_KEY;
        idx->key_len = max_len;
    }
}

/* ---- Incremental build: insert records one-by-one (O(1) extra memory) ---- */
static void index_build_incremental(index_t *idx, dbf_t *db, expr_ctx_t *ctx,
                                     const char *key_expr) {
    uint32_t i;
    for (i = 1; i <= db->record_count; i++) {
        value_t val;
        char keybuf[MAX_INDEX_KEY + 1];

        dbf_read_record(db, i);
        if (db->record_buf[0] == '*') continue;

        if (idx->for_ast) {
            value_t fcond;
            if (ast_eval(idx->for_ast, ctx, &fcond) != 0) continue;
            if (fcond.type == VAL_LOGIC && !fcond.logic) continue;
            if (fcond.type == VAL_NUM && fcond.num == 0.0) continue;
        }

        if (idx->key_ast) {
            if (ast_eval(idx->key_ast, ctx, &val) != 0) continue;
        } else {
            if (expr_eval_str(ctx, key_expr, &val) != 0) continue;
        }

        index_format_key_value(idx->key_type, &val, keybuf, sizeof(keybuf));
        index_insert(idx, keybuf, i);
    }
}

/* ---- Build index from database ---- */
int index_build(index_t *idx, dbf_t *db, expr_ctx_t *ctx, const char *key_expr, const char *filename,
                const char *for_expr, int descending) {
    uint32_t i;
    dbf_t *saved_db;
    int max_len = 0;
    int key_type_set = 0;
    key_entry_t *entries = NULL;
    int nentries = 0;
    ndx_page_t **leaf_pages = NULL;
    ndx_page_t **current_level = NULL;

    if (!dbf_is_open(db)) return -1;

    /* Close any previous state */
    free_all_pages(idx);
    if (idx->fp) { fclose(idx->fp); idx->fp = NULL; }

    char norm_path[64];
    str_copy(norm_path, filename, sizeof(norm_path));
    path_normalize(norm_path);

    str_copy(idx->key_expr, key_expr, sizeof(idx->key_expr));
    str_copy(idx->filename, norm_path, sizeof(idx->filename));

    /* Store FOR/DESCENDING */
    if (for_expr && for_expr[0]) {
        str_copy(idx->for_expr, for_expr, sizeof(idx->for_expr));
        const char *ast_err;
        if (idx->for_ast) { ast_free(idx->for_ast); idx->for_ast = NULL; }
        idx->for_ast = ast_compile(for_expr, ctx->vars, &ast_err);
    } else {
        idx->for_expr[0] = '\0';
        if (idx->for_ast) { ast_free(idx->for_ast); idx->for_ast = NULL; }
    }
    idx->descending = descending;

    if (index_compile_key_ast(idx, ctx->vars) < 0) {
        /* Keep dynamic fallback if AST compile fails. */
        idx->key_ast = NULL;
    }

    saved_db = ctx->db;
    ctx->db = db;

    /* Try to allocate the bulk entries array */
    if (db->record_count > 0) {
        entries = (key_entry_t *)malloc(db->record_count * sizeof(key_entry_t));
    }

    if (!entries && db->record_count > 0) {
        /* ---- INCREMENTAL PATH: bounded memory ---- */
        index_probe_key_info(idx, db, ctx, key_expr);
        compute_fanout(idx);

        /* Initialize empty tree */
        idx->num_pages = 0;
        idx->nentries = 0;
        idx->root_page = 0;
        idx->first_leaf = 0;
        {
            ndx_page_t *hdr = (ndx_page_t *)calloc(1, sizeof(ndx_page_t));
            hdr->page_no = 0;
            hdr->pin_count = 1;
            hash_add(idx, hdr);
            idx->num_pages = 1;
        }

        /* Open file before inserts so cache_evict can flush dirty pages */
        idx->fp = fopen(norm_path, "w+b");
        if (!idx->fp) {
            free_all_pages(idx);
            ctx->db = saved_db;
            return -1;
        }
        idx->active = 1;

        index_build_incremental(idx, db, ctx, key_expr);

        ctx->db = saved_db;
        if (idx->nentries > 0) {
            index_top(idx);
        } else {
            idx->iter_page = -1;
            idx->iter_pos = -1;
        }
        return 0;
    }

    /* ---- BULK PATH: fast sort+pack ---- */
    idx->key_len = 10;
    idx->key_type = 0;

    for (i = 1; i <= db->record_count; i++) {
        value_t val;
        char keybuf[MAX_INDEX_KEY + 1];

        dbf_read_record(db, i);
        if (db->record_buf[0] == '*') continue;

        /* Apply FOR filter */
        if (idx->for_ast) {
            value_t fcond;
            if (ast_eval(idx->for_ast, ctx, &fcond) != 0) continue;
            if (fcond.type == VAL_LOGIC && !fcond.logic) continue;
            if (fcond.type == VAL_NUM && fcond.num == 0.0) continue;
        }

        if (idx->key_ast) {
            if (ast_eval(idx->key_ast, ctx, &val) != 0) continue;
        } else {
            if (expr_eval_str(ctx, key_expr, &val) != 0) continue;
        }

        if (!key_type_set) {
            if (val.type == VAL_NUM) idx->key_type = 1;
            else if (val.type == VAL_DATE) idx->key_type = 2;
            else idx->key_type = 0;
            key_type_set = 1;
        }

        index_format_key_value(idx->key_type, &val, keybuf, sizeof(keybuf));
        {
            int len = strlen(keybuf);
            if (idx->key_type == 0 && len > max_len) max_len = len;

            /* Store entry for sorting */
            memset(entries[nentries].key, ' ', MAX_INDEX_KEY);
            if (len > MAX_INDEX_KEY) len = MAX_INDEX_KEY;
            memcpy(entries[nentries].key, keybuf, len);
            entries[nentries].recno = i;
            nentries++;
        }
    }

    if (!key_type_set) {
        /* No records — fall back to schema-based probe */
        index_probe_key_from_schema(idx, db, key_expr);
    } else if (idx->key_type == 1) {
        idx->key_len = 16; /* sortable transformed IEEE-754 hex */
    } else if (idx->key_type == 2) {
        idx->key_len = 8;  /* canonical YYYYMMDD */
    } else {
        if (max_len < 1) max_len = 1;
        if (max_len > MAX_INDEX_KEY) max_len = MAX_INDEX_KEY;
        idx->key_len = max_len;
    }

    compute_fanout(idx);

    /* Sort entries */
    if (nentries > 0) {
        qsort_r(entries, nentries, sizeof(key_entry_t), key_entry_cmp, idx);
    }

    if (idx->unique && nentries > 1) {
        int out = 0;
        int j;
        for (i = 0; i < (uint32_t)nentries;) {
            uint32_t recno = entries[i].recno;
            j = (int)i + 1;
            while (j < nentries &&
                   key_cmp(entries[i].key, entries[j].key, idx->key_len) == 0) {
                if (entries[j].recno < recno) recno = entries[j].recno;
                j++;
            }
            if (out != (int)i) memcpy(&entries[out], &entries[i], sizeof(entries[0]));
            entries[out].recno = recno;
            out++;
            i = (uint32_t)j;
        }
        nentries = out;
    }

    /* Pack into leaf pages and build tree bottom-up */
    idx->num_pages = 0;
    idx->nentries = 0;
    idx->root_page = 0;
    idx->first_leaf = 0;

    /* Initialize header page (0) */
    {
        ndx_page_t *hdr = (ndx_page_t *)calloc(1, sizeof(ndx_page_t));
        hdr->page_no = 0;
        hdr->pin_count = 1;
        hash_add(idx, hdr);
        idx->num_pages = 1;
    }

    if (nentries > 0) {
        int n_leaf_pages = (nentries + idx->max_keys_leaf - 1) / idx->max_keys_leaf;
        leaf_pages = (ndx_page_t **)malloc(n_leaf_pages * sizeof(ndx_page_t *));
        int current_entry = 0;
        if (!leaf_pages) goto fail;

        for (i = 0; i < (uint32_t)n_leaf_pages; i++) {
            ndx_page_t *leaf = page_new(idx, PAGE_LEAF);
            if (!leaf) goto fail;
            leaf_pages[i] = leaf;
            if (i == 0) idx->first_leaf = leaf->page_no;
            int count_in_page = idx->max_keys_leaf;
            if (current_entry + count_in_page > nentries)
                count_in_page = nentries - current_entry;

            for (int j = 0; j < count_in_page; j++) {
                memcpy(page_key(idx, leaf, j), entries[current_entry].key, idx->key_len);
                leaf->recnos[j] = entries[current_entry].recno;
                current_entry++;
            }
            leaf->nkeys = count_in_page;
            leaf->dirty = 1;
            idx->nentries += count_in_page;

            if (i > 0) {
                leaf->prev_leaf = leaf_pages[i-1]->page_no;
                leaf_pages[i-1]->next_leaf = leaf->page_no;
            }
        }

        /* Build internal levels on top of leaves */
        current_level = leaf_pages;
        int current_level_size = n_leaf_pages;

        while (current_level_size > 1) {
            int parent_level_size = (current_level_size + idx->max_keys_internal) / (idx->max_keys_internal + 1);
            if (parent_level_size < 1) parent_level_size = 1;
            ndx_page_t **parent_level = (ndx_page_t **)malloc(parent_level_size * sizeof(ndx_page_t *));
            int child_idx = 0;
            if (!parent_level) goto fail;

            for (i = 0; i < (uint32_t)parent_level_size; i++) {
                ndx_page_t *parent = page_new(idx, PAGE_INTERNAL);
                if (!parent) {
                    free(parent_level);
                    goto fail;
                }
                parent_level[i] = parent;

                int children_for_this_parent = idx->max_keys_internal + 1;
                if (child_idx + children_for_this_parent > current_level_size)
                    children_for_this_parent = current_level_size - child_idx;

                parent->children[0] = current_level[child_idx]->page_no;
                for (int j = 0; j < children_for_this_parent - 1; j++) {
                    /* Key in parent is the first key of the right sibling subtree */
                    ndx_page_t *right_child = current_level[child_idx + j + 1];
                    /* Find first leaf key in right_child subtree */
                    ndx_page_t *curr = right_child;
                    while (curr->type == PAGE_INTERNAL) {
                        curr = hash_lookup(idx, curr->children[0]);
                        if (!curr) {
                            free(parent_level);
                            goto fail;
                        }
                    }
                    memcpy(page_key(idx, parent, j), page_key(idx, curr, 0), idx->key_len);
                    parent->children[j + 1] = right_child->page_no;
                    parent->nkeys++;
                }
                parent->dirty = 1;
                child_idx += children_for_this_parent;
            }

            for (int k = 0; k < current_level_size; k++) {
                page_put(current_level[k]);
            }
            free(current_level);
            current_level = parent_level;
            current_level_size = parent_level_size;
        }

        idx->root_page = current_level[0]->page_no;
        page_put(current_level[0]);
        free(current_level);
        current_level = NULL;
    } else {
        /* Empty index - already has header, but needs a root leaf */
        ndx_page_t *leaf = page_new(idx, PAGE_LEAF);
        idx->root_page = leaf->page_no;
        idx->first_leaf = leaf->page_no;
        page_put(leaf);
    }

    free(entries);
    ctx->db = saved_db;

    /* Position at first entry */
    if (idx->nentries > 0) {
        index_top(idx);
    } else {
        idx->iter_page = -1;
        idx->iter_pos = -1;
    }

    idx->active = 1;
    return 0;

fail:
    index_clear_key_ast(idx);
    if (entries) free(entries);
    if (current_level && current_level != leaf_pages) free(current_level);
    if (leaf_pages) free(leaf_pages);
    free_all_pages(idx);
    ctx->db = saved_db;
    return -1;
}

/* ---- Write index to file ---- */
int index_write(index_t *idx) {
    int i;

    if (idx->fp) {
        fclose(idx->fp);
        idx->fp = NULL;
    }

    idx->fp = fopen(idx->filename, "wb");
    if (!idx->fp) return -1;

    /* Write all pages */
    for (i = 0; i < idx->num_pages; i++) {
        if (page_flush(idx, i) < 0) {
            fclose(idx->fp);
            idx->fp = NULL;
            return -1;
        }
    }

    /* Keep file open for incremental writes */
    return 0;
}

/* ---- Public: Flush dirty pages to disk ---- */
void index_flush(index_t *idx) {
    ndx_page_t *p;
    if (!idx->fp) {
        idx->fp = fopen(idx->filename, "r+b");
        if (!idx->fp) return;
    }
    /* Always write header */
    page_flush(idx, 0);
    for (p = idx->lru_head; p; p = p->lru_next) {
        if (p->dirty)
            page_flush(idx, p->page_no);
    }
    fflush(idx->fp);
}

/* ---- Read NDX2 format ---- */
static int read_ndx2(index_t *idx, FILE *fp) {
    unsigned char buf[NDX_PAGE_SIZE];
    uint32_t tmp;

    /* Parse header (already read first 4 bytes for magic) */
    fseek(fp, 0, SEEK_SET);
    if (fread(buf, NDX_PAGE_SIZE, 1, fp) != 1) return -1;

    memcpy(&tmp, buf + 4, 4);  /* version */
    memcpy(&tmp, buf + 8, 4);  idx->root_page = (int)tmp;
    memcpy(&tmp, buf + 12, 4); idx->first_leaf = (int)tmp;
    memcpy(&tmp, buf + 16, 4); idx->num_pages = (int)tmp;
    memcpy(&tmp, buf + 20, 4); idx->nentries = (int)tmp;
    memcpy(&tmp, buf + 24, 4); idx->key_len = (int)tmp;
    memcpy(&tmp, buf + 28, 4); idx->key_type = (int)tmp;
    memcpy(&tmp, buf + 32, 4); idx->free_page_head = (int)tmp;
    memcpy(&tmp, buf + 36, 4); idx->unique = (int)tmp;
    memcpy(idx->key_expr, buf + 40, 256);
    idx->key_expr[255] = '\0';
    memcpy(idx->for_expr, buf + 296, 256);
    idx->for_expr[255] = '\0';
    memcpy(&tmp, buf + 552, 4); idx->descending = (int)tmp;

    compute_fanout(idx);

    /* Page 0 = header (dummy) */
    {
        ndx_page_t *hdr = (ndx_page_t *)calloc(1, sizeof(ndx_page_t));
        hdr->page_no = 0;
        hdr->pin_count = 1;
        hash_add(idx, hdr);
    }

    return 0;
}

/* ---- Read NDX1 (old flat format) ---- */
static int read_ndx1(index_t *idx, FILE *fp) {
    uint32_t nentries, keylen;
    int i;
    char *keys;
    uint32_t *recnos;

    /* Header: magic(4) already read, nentries(4), keylen(4), expr(256) */
    fseek(fp, 4, SEEK_SET);
    if (fread(&nentries, 4, 1, fp) != 1) return -1;
    if (fread(&keylen, 4, 1, fp) != 1) return -1;

    {
        char expr_buf[256];
        if (fread(expr_buf, 256, 1, fp) != 1) return -1;
        str_copy(idx->key_expr, expr_buf, sizeof(idx->key_expr));
    }

    /* Read flat entries */
    keys = (char *)malloc(nentries * keylen);
    recnos = (uint32_t *)malloc(nentries * sizeof(uint32_t));
    if (!keys || !recnos) {
        if (keys) free(keys);
        if (recnos) free(recnos);
        return -1;
    }

    for (i = 0; i < (int)nentries; i++) {
        if (fread(keys + i * keylen, keylen, 1, fp) != 1) break;
        if (fread(&recnos[i], 4, 1, fp) != 1) break;
    }
    nentries = i;

    /* Determine actual key_len from old data */
    idx->key_len = (int)keylen;
    if (idx->key_len > MAX_INDEX_KEY) idx->key_len = MAX_INDEX_KEY;
    idx->key_type = 0;
    idx->unique = 0;
    compute_fanout(idx);

    /* Initialize empty tree */
    {
        ndx_page_t *hdr = (ndx_page_t *)calloc(1, sizeof(ndx_page_t));
        hdr->page_no = 0;
        hdr->pin_count = 1;
        hash_add(idx, hdr);
    }
    idx->num_pages = 1;
    idx->root_page = 0;
    idx->first_leaf = 0;
    idx->nentries = 0;

    /* Bulk insert (data is already sorted) */
    for (i = 0; i < (int)nentries; i++) {
        char keybuf[MAX_INDEX_KEY + 1];
        memset(keybuf, ' ', idx->key_len);
        memcpy(keybuf, keys + i * keylen, idx->key_len);
        keybuf[idx->key_len] = '\0';
        index_insert(idx, keybuf, recnos[i]);
    }

    free(keys);
    free(recnos);
    return 0;
}

/* ---- Read index from file ---- */
int index_read(index_t *idx, const char *filename) {
    FILE *fp;
    uint32_t magic;
    char norm_path[64];

    str_copy(norm_path, filename, sizeof(norm_path));
    path_normalize(norm_path);

    fp = fopen(norm_path, "rb");
    if (!fp) return -1;

    if (fread(&magic, 4, 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    str_copy(idx->filename, norm_path, sizeof(idx->filename));

    if (magic == NDX2_MAGIC) {
        if (read_ndx2(idx, fp) < 0) {
            fclose(fp);
            return -1;
        }
    } else if (magic == NDX1_MAGIC) {
        if (read_ndx1(idx, fp) < 0) {
            fclose(fp);
            return -1;
        }
    } else {
        fclose(fp);
        return -1;
    }

    fclose(fp);

    /* Open for incremental writes */
    idx->fp = fopen(filename, "r+b");

    /* Position at first entry */
    if (idx->nentries > 0) {
        index_top(idx);
    } else {
        idx->iter_page = -1;
        idx->iter_pos = -1;
    }

    idx->active = 1;
    return 0;
}
