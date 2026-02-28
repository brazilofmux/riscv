#ifndef LEX_H
#define LEX_H

#include <stdint.h>
#include "expr.h" 
#include "memvar.h"

typedef enum {
    TOK_EOF = 0,
    TOK_ERROR,
    
    /* Literals */
    TOK_IDENT,
    TOK_STRING,
    TOK_NUMBER,
    TOK_DATE,
    TOK_LOGIC,
    
    /* Operators */
    TOK_PLUS,       /* + */
    TOK_MINUS,      /* - */
    TOK_MUL,        /* * */
    TOK_DIV,        /* / */
    TOK_LPAREN,     /* ( */
    TOK_RPAREN,     /* ) */
    TOK_LBRACKET,   /* [ */
    TOK_RBRACKET,   /* ] */
    TOK_COMMA,      /* , */
    TOK_ARROW,      /* -> */
    
    /* Comparison */
    TOK_EQ,         /* = */
    TOK_NE,         /* <> or # */
    TOK_LT,         /* < */
    TOK_GT,         /* > */
    TOK_LE,         /* <= */
    TOK_GE,         /* >= */
    TOK_SUBSTR,     /* $ */
    TOK_EXACT_EQ,   /* == */
    TOK_POWER,      /* ** or ^ */

    /* Logical Keywords (delimited by dots) */
    TOK_AND,        /* .AND. */
    TOK_OR,         /* .OR. */
    TOK_NOT,        /* .NOT. or ! */
    
    /* Macro */
    TOK_MACRO,      /* & */
    TOK_DOT,        /* . */
    TOK_UNKNOWN
} token_type_t;

typedef struct {
    token_type_t type;
    char text[256];
    double num_val;
    int logic_val;
    int32_t date_val;
} token_t;

#define MAX_MACRO_NESTING 8

typedef struct lexer {
    const char *input;
    const char *p;
    const char *token_start;
    token_t current;
    token_type_t prev_type;  /* previous token type (for context-sensitive '[') */
    const char *error;

    /* Macro expansion state */
    memvar_store_t *store;
    struct {
        char buf[256];
        const char *p;
    } macro_stack[MAX_MACRO_NESTING];
    int macro_depth;
} lexer_t;

void lexer_init(lexer_t *l, const char *input);
void lexer_init_ext(lexer_t *l, const char *input, memvar_store_t *store);
token_type_t lex_next(lexer_t *l);
token_type_t lex_peek(lexer_t *l);

/* Return remaining unconsumed input, expanding macros if necessary.
   Result is stored in out_buf. */
void lex_get_remaining(lexer_t *l, char *out_buf, int size);

/* Helper to check for keywords with 4-char rule */
int is_keyword(const char *ident, const char *kw);
#define IS_KW(ident, kw) is_keyword(ident, kw)

/* Check if an identifier is a core reserved keyword */
int lex_is_reserved(const char *ident);

#endif
