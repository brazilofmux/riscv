#ifndef PROLOG_TERM_H
#define PROLOG_TERM_H

#include <stdint.h>

/* Tagged 32-bit term representation */
typedef int32_t term_t;

/* Current implementation-wide arity cap for compound terms. */
#define PROLOG_MAX_ARITY 32

#define TAG(t)       ((t) & 3)
#define TAG_PTR  0
#define TAG_INT  1
#define TAG_ATOM 2
#define TAG_VAR  3

#define MK_INT(n)    (((int32_t)(n) << 2) | TAG_INT)
#define UN_INT(t)    ((t) >> 2)
#define MK_ATOM(id)  (((int32_t)(id) << 2) | TAG_ATOM)
#define UN_ATOM(t)   ((uint32_t)(t) >> 2)
#define MK_VAR(id)   (((int32_t)(id) << 2) | TAG_VAR)
#define UN_VAR(t)    ((uint32_t)(t) >> 2)
#define IS_PTR(t)    (TAG(t) == TAG_PTR && (t) != 0)

#define TERM_NIL 0  /* null/invalid term */

/* Heap for compound terms */
#define HEAP_SIZE (256 * 1024)
extern term_t heap[];
extern int hp;

/* Code heap for stored clauses */
#define CODE_HEAP_SIZE (128 * 1024)
extern term_t code_heap[];
extern int code_hp;

/* Atom table */
#define MAX_ATOMS 2048
extern char *atom_names[];
extern int atom_count;

/* Well-known atom IDs (set during init) */
extern int ATOM_NIL_LIST;   /* [] */
extern int ATOM_DOT;        /* . */
extern int ATOM_TRUE;
extern int ATOM_FAIL;
extern int ATOM_CLAUSE;     /* :- */
extern int ATOM_QUERY;      /* ?- */
extern int ATOM_COMMA;
extern int ATOM_SEMI;       /* ; */
extern int ATOM_ARROW;      /* -> */
extern int ATOM_NOT;        /* \+ */
extern int ATOM_IS;
extern int ATOM_UNIFY;      /* = */
extern int ATOM_NOT_UNIFY;  /* \= */
extern int ATOM_EQ;         /* == */
extern int ATOM_NEQ;        /* \== */
extern int ATOM_CUT;        /* ! */
extern int ATOM_PLUS;
extern int ATOM_MINUS;
extern int ATOM_STAR;
extern int ATOM_SLASH2;     /* // */
extern int ATOM_MOD;
extern int ATOM_ABS;
extern int ATOM_MIN;
extern int ATOM_MAX;
extern int ATOM_LT;         /* < */
extern int ATOM_GT;         /* > */
extern int ATOM_LE;         /* =< */
extern int ATOM_GE;         /* >= */
extern int ATOM_ARITH_EQ;   /* =:= */
extern int ATOM_ARITH_NEQ;  /* =\= */
extern int ATOM_WRITE;
extern int ATOM_WRITELN;
extern int ATOM_NL;
extern int ATOM_HALT;
extern int ATOM_READ;
extern int ATOM_ASSERT;
extern int ATOM_ASSERTA;
extern int ATOM_ASSERTZ;
extern int ATOM_RETRACT;
extern int ATOM_FINDALL;
extern int ATOM_ATOM;
extern int ATOM_INTEGER;
extern int ATOM_VAR;
extern int ATOM_NONVAR;
extern int ATOM_COMPOUND;
extern int ATOM_NUMBER;
extern int ATOM_IS_LIST;
extern int ATOM_FUNCTOR;
extern int ATOM_ARG;
extern int ATOM_UNIV;       /* =.. */
extern int ATOM_COPY_TERM;
extern int ATOM_ATOM_LENGTH;
extern int ATOM_ATOM_CHARS;
extern int ATOM_CHAR_CODE;
extern int ATOM_NUMBER_CHARS;
extern int ATOM_ATOM_CONCAT;
extern int ATOM_WRITE_CANONICAL;
extern int ATOM_SUCC;
extern int ATOM_PLUS2;      /* plus */
extern int ATOM_LENGTH;
extern int ATOM_APPEND;
extern int ATOM_LAST;
extern int ATOM_BETWEEN;

/* Error handling */
#define ERRMSG_SIZE 256
extern int g_error;
extern char g_errmsg[ERRMSG_SIZE];

/* Functions */
void init_atoms(void);
int atom_intern(const char *name);
const char *atom_name(int id);

/* Heap allocation */
term_t heap_alloc(int n);
term_t code_heap_alloc(int n);
term_t make_compound(int functor_atom, int arity, term_t *args);
term_t make_compound_on_code(int functor_atom, int arity, term_t *args);
term_t make_list_cons(term_t head, term_t tail);

/* Compound accessors (t must be IS_PTR) */
int compound_functor(term_t t);
int compound_arity(term_t t);
term_t compound_arg(term_t t, int i); /* 0-based */
void compound_set_arg(term_t t, int i, term_t val);

/* Functor/arity of any callable term */
int term_functor(term_t t);
int term_arity(term_t t);

/* Deep-copy a term from working heap to persistent code heap */
term_t persist_term(term_t t);

#endif
