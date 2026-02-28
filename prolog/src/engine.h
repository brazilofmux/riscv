#ifndef PROLOG_ENGINE_H
#define PROLOG_ENGINE_H

#include "term.h"

/* Variable bindings */
#define MAX_VARS (32 * 1024)
extern term_t var_binding[];
extern int var_count;

/* Trail */
#define TRAIL_SIZE (64 * 1024)
extern int trail[];
extern int trail_top;

/* Goal stack */
#define GOAL_STACK_SIZE (16 * 1024)
extern term_t goal_stack[];
extern int goal_sp;

typedef struct {
    int trail_top;
    int hp;
    int var_count;
    void *next_clause;  /* clause_t* */
    term_t goal;
    int cut_barrier;
    term_t continuation; /* remaining goals as conjunction */
} choice_t;

#define MAX_CHOICES 2048
extern choice_t choices[];
extern int choice_top;

/* Cut barrier */
extern int cut_barrier;

/* Functions */
term_t deref(term_t t);
void bind(int var_id, term_t val);
void trail_undo(int saved_top);

int unify(term_t a, term_t b);

term_t fresh_var(void);
term_t copy_term(term_t t, int *var_map, int map_size);
term_t copy_term_code(term_t t, int *var_map, int map_size);

int solve(term_t goal);

void engine_reset(void);

#endif
