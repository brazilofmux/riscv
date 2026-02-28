#ifndef PROLOG_READER_H
#define PROLOG_READER_H

/* Token types */
enum {
    TOK_ATOM,
    TOK_VAR,
    TOK_INT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_DOT,
    TOK_COMMA,
    TOK_BAR,
    TOK_STRING,
    TOK_EOF,
    TOK_ERROR
};

#define TOK_BUF_SIZE 256

typedef struct {
    int type;
    char text[TOK_BUF_SIZE];
    int int_val;
} token_t;

extern token_t cur_tok;

void reader_init(void);
void next_token(void);

#endif
