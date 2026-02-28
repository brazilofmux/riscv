#include "term.h"
#include <string.h>
#include <stdio.h>

/* Heap — index 0 is reserved (TERM_NIL == 0 collides with ptr to index 0) */
term_t heap[HEAP_SIZE];
int hp = 1;

/* Code heap — index 0 reserved for same reason */
term_t code_heap[CODE_HEAP_SIZE];
int code_hp = 1;

/* Atom table */
char *atom_names[MAX_ATOMS];
int atom_count = 0;

/* Well-known atoms */
int ATOM_NIL_LIST, ATOM_DOT, ATOM_TRUE, ATOM_FAIL;
int ATOM_CLAUSE, ATOM_QUERY, ATOM_COMMA, ATOM_SEMI;
int ATOM_ARROW, ATOM_NOT, ATOM_IS, ATOM_UNIFY;
int ATOM_NOT_UNIFY, ATOM_EQ, ATOM_NEQ, ATOM_CUT;
int ATOM_PLUS, ATOM_MINUS, ATOM_STAR, ATOM_SLASH2, ATOM_MOD;
int ATOM_ABS, ATOM_MIN, ATOM_MAX;
int ATOM_LT, ATOM_GT, ATOM_LE, ATOM_GE;
int ATOM_ARITH_EQ, ATOM_ARITH_NEQ;
int ATOM_WRITE, ATOM_WRITELN, ATOM_NL, ATOM_HALT, ATOM_READ;
int ATOM_ASSERT, ATOM_ASSERTA, ATOM_ASSERTZ, ATOM_RETRACT;
int ATOM_FINDALL;
int ATOM_ATOM, ATOM_INTEGER, ATOM_VAR, ATOM_NONVAR;
int ATOM_COMPOUND, ATOM_NUMBER, ATOM_IS_LIST;
int ATOM_FUNCTOR, ATOM_ARG, ATOM_UNIV, ATOM_COPY_TERM;
int ATOM_ATOM_LENGTH, ATOM_ATOM_CHARS, ATOM_CHAR_CODE;
int ATOM_NUMBER_CHARS, ATOM_ATOM_CONCAT;
int ATOM_WRITE_CANONICAL, ATOM_SUCC, ATOM_PLUS2;
int ATOM_LENGTH, ATOM_APPEND, ATOM_LAST, ATOM_BETWEEN;

/* Error handling */
int g_error = 0;
char g_errmsg[ERRMSG_SIZE];

/* Static atom name storage */
#define ATOM_STORE_SIZE (32 * 1024)
static char atom_store[ATOM_STORE_SIZE];
static int atom_store_pos = 0;

static char *store_string(const char *s) {
    int len = strlen(s) + 1;
    if (atom_store_pos + len > ATOM_STORE_SIZE) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "atom store full");
        return 0;
    }
    char *p = &atom_store[atom_store_pos];
    memcpy(p, s, len);
    atom_store_pos += len;
    return p;
}

int atom_intern(const char *name) {
    int i;
    for (i = 0; i < atom_count; i++) {
        if (strcmp(atom_names[i], name) == 0)
            return i;
    }
    if (atom_count >= MAX_ATOMS) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "too many atoms");
        return 0;
    }
    atom_names[atom_count] = store_string(name);
    if (!atom_names[atom_count]) return 0;
    return atom_count++;
}

const char *atom_name(int id) {
    if (id < 0 || id >= atom_count) return "?";
    return atom_names[id];
}

void init_atoms(void) {
    atom_count = 0;
    atom_store_pos = 0;

    ATOM_NIL_LIST = atom_intern("[]");
    ATOM_DOT = atom_intern(".");
    ATOM_TRUE = atom_intern("true");
    ATOM_FAIL = atom_intern("fail");
    ATOM_CLAUSE = atom_intern(":-");
    ATOM_QUERY = atom_intern("?-");
    ATOM_COMMA = atom_intern(",");
    ATOM_SEMI = atom_intern(";");
    ATOM_ARROW = atom_intern("->");
    ATOM_NOT = atom_intern("\\+");
    ATOM_IS = atom_intern("is");
    ATOM_UNIFY = atom_intern("=");
    ATOM_NOT_UNIFY = atom_intern("\\=");
    ATOM_EQ = atom_intern("==");
    ATOM_NEQ = atom_intern("\\==");
    ATOM_CUT = atom_intern("!");
    ATOM_PLUS = atom_intern("+");
    ATOM_MINUS = atom_intern("-");
    ATOM_STAR = atom_intern("*");
    ATOM_SLASH2 = atom_intern("//");
    ATOM_MOD = atom_intern("mod");
    ATOM_ABS = atom_intern("abs");
    ATOM_MIN = atom_intern("min");
    ATOM_MAX = atom_intern("max");
    ATOM_LT = atom_intern("<");
    ATOM_GT = atom_intern(">");
    ATOM_LE = atom_intern("=<");
    ATOM_GE = atom_intern(">=");
    ATOM_ARITH_EQ = atom_intern("=:=");
    ATOM_ARITH_NEQ = atom_intern("=\\=");
    ATOM_WRITE = atom_intern("write");
    ATOM_WRITELN = atom_intern("writeln");
    ATOM_NL = atom_intern("nl");
    ATOM_HALT = atom_intern("halt");
    ATOM_READ = atom_intern("read");
    ATOM_ASSERT = atom_intern("assert");
    ATOM_ASSERTA = atom_intern("asserta");
    ATOM_ASSERTZ = atom_intern("assertz");
    ATOM_RETRACT = atom_intern("retract");
    ATOM_FINDALL = atom_intern("findall");
    ATOM_ATOM = atom_intern("atom");
    ATOM_INTEGER = atom_intern("integer");
    ATOM_VAR = atom_intern("var");
    ATOM_NONVAR = atom_intern("nonvar");
    ATOM_COMPOUND = atom_intern("compound");
    ATOM_NUMBER = atom_intern("number");
    ATOM_IS_LIST = atom_intern("is_list");
    ATOM_FUNCTOR = atom_intern("functor");
    ATOM_ARG = atom_intern("arg");
    ATOM_UNIV = atom_intern("=..");
    ATOM_COPY_TERM = atom_intern("copy_term");
    ATOM_ATOM_LENGTH = atom_intern("atom_length");
    ATOM_ATOM_CHARS = atom_intern("atom_chars");
    ATOM_CHAR_CODE = atom_intern("char_code");
    ATOM_NUMBER_CHARS = atom_intern("number_chars");
    ATOM_ATOM_CONCAT = atom_intern("atom_concat");
    ATOM_WRITE_CANONICAL = atom_intern("write_canonical");
    ATOM_SUCC = atom_intern("succ");
    ATOM_PLUS2 = atom_intern("plus");
    ATOM_LENGTH = atom_intern("length");
    ATOM_APPEND = atom_intern("append");
    ATOM_LAST = atom_intern("last");
    ATOM_BETWEEN = atom_intern("between");
}

term_t heap_alloc(int n) {
    if (hp + n > HEAP_SIZE) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "heap overflow");
        return 0;
    }
    int p = hp;
    hp += n;
    return p;
}

term_t code_heap_alloc(int n) {
    if (code_hp + n > CODE_HEAP_SIZE) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "code heap overflow");
        return 0;
    }
    int p = code_hp;
    code_hp += n;
    return p;
}

term_t make_compound(int functor_atom, int arity, term_t *args) {
    if (arity < 0 || arity > PROLOG_MAX_ARITY) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "arity overflow");
        return TERM_NIL;
    }
    int p = heap_alloc(2 + arity);
    if (g_error) return TERM_NIL;
    heap[p] = MK_ATOM(functor_atom);
    heap[p + 1] = MK_INT(arity);
    int i;
    for (i = 0; i < arity; i++)
        heap[p + 2 + i] = args[i];
    return (p << 2) | TAG_PTR;
}

term_t make_compound_on_code(int functor_atom, int arity, term_t *args) {
    if (arity < 0 || arity > PROLOG_MAX_ARITY) {
        g_error = 1;
        snprintf(g_errmsg, sizeof(g_errmsg), "arity overflow");
        return TERM_NIL;
    }
    int p = code_heap_alloc(2 + arity);
    if (g_error) return TERM_NIL;
    code_heap[p] = MK_ATOM(functor_atom);
    code_heap[p + 1] = MK_INT(arity);
    int i;
    for (i = 0; i < arity; i++)
        code_heap[p + 2 + i] = args[i];
    /* Offset by HEAP_SIZE so compound_base can distinguish code vs working heap */
    return ((HEAP_SIZE + p) << 2) | TAG_PTR;
}

term_t make_list_cons(term_t head, term_t tail) {
    term_t args[2];
    args[0] = head;
    args[1] = tail;
    return make_compound(ATOM_DOT, 2, args);
}

/* Decode a PTR to get the heap/code_heap index */
static int ptr_index(term_t t) {
    return (t >> 2);
}

/* Get the base pointer to the compound's storage.
   Indices >= HEAP_SIZE are on the code heap (offset by HEAP_SIZE). */
static term_t *compound_base(term_t t) {
    int idx = ptr_index(t);
    if (idx >= HEAP_SIZE)
        return &code_heap[idx - HEAP_SIZE];
    return &heap[idx];
}

int compound_functor(term_t t) {
    term_t *base = compound_base(t);
    return UN_ATOM(base[0]);
}

int compound_arity(term_t t) {
    term_t *base = compound_base(t);
    return UN_INT(base[1]);
}

term_t compound_arg(term_t t, int i) {
    term_t *base = compound_base(t);
    return base[2 + i];
}

void compound_set_arg(term_t t, int i, term_t val) {
    term_t *base = compound_base(t);
    base[2 + i] = val;
}

int term_functor(term_t t) {
    if (TAG(t) == TAG_ATOM) return UN_ATOM(t);
    if (IS_PTR(t)) return compound_functor(t);
    return -1;
}

int term_arity(term_t t) {
    if (TAG(t) == TAG_ATOM) return 0;
    if (IS_PTR(t)) return compound_arity(t);
    return -1;
}

/* Deep-copy a term from working heap to code heap (persistent storage) */
term_t persist_term(term_t t) {
    switch (TAG(t)) {
    case TAG_INT:
    case TAG_ATOM:
    case TAG_VAR:
        return t; /* these are self-contained values */
    default: /* TAG_PTR */
        if (!IS_PTR(t)) return t;
        {
            int func = compound_functor(t);
            int arity = compound_arity(t);
            if (arity < 0 || arity > PROLOG_MAX_ARITY) {
                g_error = 1;
                snprintf(g_errmsg, sizeof(g_errmsg), "arity overflow");
                return TERM_NIL;
            }
            term_t args[PROLOG_MAX_ARITY];
            int i;
            for (i = 0; i < arity; i++)
                args[i] = persist_term(compound_arg(t, i));
            return make_compound_on_code(func, arity, args);
        }
    }
}
