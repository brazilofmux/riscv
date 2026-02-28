#ifndef PROLOG_DATABASE_H
#define PROLOG_DATABASE_H

#include "term.h"

typedef struct clause {
    term_t head;
    term_t body;
    int num_vars;
    struct clause *next;
} clause_t;

typedef struct predicate {
    int functor;
    int arity;
    clause_t *first;
    clause_t *last;
    struct predicate *hash_next;
} predicate_t;

#define PRED_HASH_SIZE 256

void db_init(void);
predicate_t *db_lookup(int functor, int arity);
void db_add_clause(term_t head, term_t body, int num_vars, int front);
int db_retract(term_t head, term_t body);
clause_t *db_alloc_clause(void);

#endif
