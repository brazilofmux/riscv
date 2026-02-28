#include "database.h"
#include "engine.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static predicate_t *pred_table[PRED_HASH_SIZE];

/* Predicate node storage (static arena) */
#define MAX_PREDICATES 512
static predicate_t pred_arena[MAX_PREDICATES];
static int pred_arena_count = 0;

/* Clause node storage (static arena) */
#define MAX_CLAUSES 4096
static clause_t clause_arena[MAX_CLAUSES];
static int clause_arena_count = 0;

static unsigned hash_pred(int functor, int arity) {
    return (unsigned)(functor * 31 + arity) % PRED_HASH_SIZE;
}

void db_init(void) {
    memset(pred_table, 0, sizeof(pred_table));
    pred_arena_count = 0;
    clause_arena_count = 0;
}

predicate_t *db_lookup(int functor, int arity) {
    unsigned h = hash_pred(functor, arity);
    predicate_t *p = pred_table[h];
    while (p) {
        if (p->functor == functor && p->arity == arity)
            return p;
        p = p->hash_next;
    }
    return 0;
}

static predicate_t *db_get_or_create(int functor, int arity) {
    predicate_t *p = db_lookup(functor, arity);
    if (p) return p;

    if (pred_arena_count >= MAX_PREDICATES) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "too many predicates");
        return 0;
    }

    p = &pred_arena[pred_arena_count++];
    p->functor = functor;
    p->arity = arity;
    p->first = 0;
    p->last = 0;

    unsigned h = hash_pred(functor, arity);
    p->hash_next = pred_table[h];
    pred_table[h] = p;
    return p;
}

clause_t *db_alloc_clause(void) {
    if (clause_arena_count >= MAX_CLAUSES) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "too many clauses");
        return 0;
    }
    clause_t *c = &clause_arena[clause_arena_count++];
    c->head = TERM_NIL;
    c->body = TERM_NIL;
    c->num_vars = 0;
    c->next = 0;
    return c;
}

void db_add_clause(term_t head, term_t body, int num_vars, int front) {
    int functor = term_functor(head);
    int arity = term_arity(head);
    if (functor < 0) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "invalid clause head");
        return;
    }

    predicate_t *pred = db_get_or_create(functor, arity);
    if (!pred) return;

    clause_t *c = db_alloc_clause();
    if (!c) return;

    /* Deep-copy terms to persistent code heap so they survive engine_reset */
    c->head = persist_term(head);
    c->body = persist_term(body);
    c->num_vars = num_vars;

    if (front) {
        c->next = pred->first;
        pred->first = c;
        if (!pred->last)
            pred->last = c;
    } else {
        c->next = 0;
        if (pred->last)
            pred->last->next = c;
        else
            pred->first = c;
        pred->last = c;
    }
}

int db_retract(term_t head, term_t body) {
    int functor = term_functor(head);
    int arity = term_arity(head);
    predicate_t *pred = db_lookup(functor, arity);
    if (!pred || !pred->first) return 0;

    clause_t *prev = 0;
    clause_t *c = pred->first;
    while (c) {
        /* Try to unify head with clause head */
        int saved_trail = trail_top;
        int saved_hp = hp;
        int saved_vc = var_count;

        /* Copy clause with fresh vars for unification */
        int var_map[256];
        memset(var_map, -1, sizeof(var_map));
        term_t ch = copy_term_code(c->head, var_map, 256);
        term_t cb = copy_term_code(c->body, var_map, 256);

        if (unify(head, ch) && (body == TERM_NIL || body == MK_ATOM(ATOM_TRUE) || unify(body, cb))) {
            /* Remove clause */
            if (prev)
                prev->next = c->next;
            else
                pred->first = c->next;
            if (pred->last == c)
                pred->last = prev;
            return 1;
        }

        /* Undo unification */
        trail_undo(saved_trail);
        hp = saved_hp;
        var_count = saved_vc;

        prev = c;
        c = c->next;
    }
    return 0;
}
