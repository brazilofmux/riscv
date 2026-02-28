#ifndef PROLOG_PARSER_H
#define PROLOG_PARSER_H

#include "term.h"

/* Per-clause variable name tracking */
#define MAX_CLAUSE_VARS 256

typedef struct {
    char name[64];
    int var_id;
    int is_named; /* 0 for _ */
} clause_var_t;

extern clause_var_t clause_vars[];
extern int clause_var_count;
extern int clause_max_var;

/* Parse a complete term (clause or query), ending at '.' */
term_t parse_term(void);

/* Parser init */
void parser_init(void);

#endif
