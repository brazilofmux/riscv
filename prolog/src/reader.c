#include "reader.h"
#include "term.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>

token_t cur_tok;

static int peek_char = -2; /* -2 = no peek */

static int peek(void) {
    if (peek_char == -2)
        peek_char = getchar();
    return peek_char;
}

static int advance(void) {
    int c;
    if (peek_char != -2) {
        c = peek_char;
        peek_char = -2;
    } else {
        c = getchar();
    }
    return c;
}

static int is_op_char(int c) {
    return c == '+' || c == '-' || c == '*' || c == '/' ||
           c == '\\' || c == '<' || c == '>' || c == '=' ||
           c == ':' || c == '?' || c == '@' || c == '#' ||
           c == '.' ||
           c == '&' || c == '^';
}

void reader_init(void) {
    peek_char = -2;
    cur_tok.type = TOK_EOF;
}

void next_token(void) {
    int c, pos;

retry:
    /* Skip whitespace and comments */
    for (;;) {
        c = peek();
        if (c == '%') {
            while (c != '\n' && c != -1) c = advance();
            continue;
        }
        if (c == '/') {
            advance();
            if (peek() == '*') {
                advance();
                int prev = 0;
                for (;;) {
                    c = advance();
                    if (c == -1) {
                        cur_tok.type = TOK_EOF;
                        return;
                    }
                    if (prev == '*' && c == '/') break;
                    prev = c;
                }
                continue;
            } else {
                /* '/' is start of operator */
                cur_tok.type = TOK_ATOM;
                cur_tok.text[0] = '/';
                pos = 1;
                while (is_op_char(peek()) && pos < TOK_BUF_SIZE - 1) {
                    cur_tok.text[pos++] = advance();
                }
                cur_tok.text[pos] = 0;
                return;
            }
        }
        if (isspace(c)) {
            advance();
            continue;
        }
        break;
    }

    c = peek();

    if (c == -1) {
        cur_tok.type = TOK_EOF;
        return;
    }

    /* Integer */
    if (isdigit(c)) {
        cur_tok.type = TOK_INT;
        pos = 0;
        while (isdigit(peek()) && pos < TOK_BUF_SIZE - 1) {
            cur_tok.text[pos++] = advance();
        }
        cur_tok.text[pos] = 0;
        cur_tok.int_val = 0;
        int i;
        for (i = 0; i < pos; i++)
            cur_tok.int_val = cur_tok.int_val * 10 + (cur_tok.text[i] - '0');
        return;
    }

    /* Variable (uppercase or _) */
    if (isupper(c) || c == '_') {
        cur_tok.type = TOK_VAR;
        pos = 0;
        while ((isalnum(peek()) || peek() == '_') && pos < TOK_BUF_SIZE - 1) {
            cur_tok.text[pos++] = advance();
        }
        cur_tok.text[pos] = 0;
        return;
    }

    /* Lowercase atom */
    if (islower(c)) {
        cur_tok.type = TOK_ATOM;
        pos = 0;
        while ((isalnum(peek()) || peek() == '_') && pos < TOK_BUF_SIZE - 1) {
            cur_tok.text[pos++] = advance();
        }
        cur_tok.text[pos] = 0;
        return;
    }

    /* Quoted atom */
    if (c == '\'') {
        advance();
        cur_tok.type = TOK_ATOM;
        pos = 0;
        for (;;) {
            c = advance();
            if (c == -1 || c == '\n') break;
            if (c == '\'') {
                if (peek() == '\'') {
                    advance();
                    if (pos < TOK_BUF_SIZE - 1)
                        cur_tok.text[pos++] = '\'';
                } else {
                    break;
                }
            } else {
                if (pos < TOK_BUF_SIZE - 1)
                    cur_tok.text[pos++] = c;
            }
        }
        cur_tok.text[pos] = 0;
        return;
    }

    /* Double-quoted string -> atom */
    if (c == '"') {
        advance();
        cur_tok.type = TOK_STRING;
        pos = 0;
        for (;;) {
            c = advance();
            if (c == -1 || c == '"') break;
            if (c == '\\') {
                c = advance();
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c == '\\') c = '\\';
                else if (c == '"') c = '"';
            }
            if (pos < TOK_BUF_SIZE - 1)
                cur_tok.text[pos++] = c;
        }
        cur_tok.text[pos] = 0;
        return;
    }

    /* Punctuation */
    if (c == '(') { advance(); cur_tok.type = TOK_LPAREN; return; }
    if (c == ')') { advance(); cur_tok.type = TOK_RPAREN; return; }
    if (c == '[') { advance(); cur_tok.type = TOK_LBRACKET; return; }
    if (c == ']') { advance(); cur_tok.type = TOK_RBRACKET; return; }
    if (c == ',') { advance(); cur_tok.type = TOK_COMMA; return; }
    if (c == '|') { advance(); cur_tok.type = TOK_BAR; return; }

    /* '!' (cut) */
    if (c == '!') {
        advance();
        cur_tok.type = TOK_ATOM;
        cur_tok.text[0] = '!';
        cur_tok.text[1] = 0;
        return;
    }

    /* '.' - end of term if followed by whitespace/EOF, else operator */
    if (c == '.') {
        advance();
        int next = peek();
        if (next == -1 || isspace(next)) {
            cur_tok.type = TOK_DOT;
            return;
        }
        /* Otherwise it's the dot operator/functor */
        cur_tok.type = TOK_ATOM;
        cur_tok.text[0] = '.';
        pos = 1;
        while (is_op_char(peek()) && pos < TOK_BUF_SIZE - 1) {
            cur_tok.text[pos++] = advance();
        }
        cur_tok.text[pos] = 0;
        return;
    }

    /* ';' */
    if (c == ';') {
        advance();
        cur_tok.type = TOK_ATOM;
        cur_tok.text[0] = ';';
        cur_tok.text[1] = 0;
        return;
    }

    /* Negative integer: - followed by digit */
    if (c == '-') {
        advance();
        if (isdigit(peek())) {
            cur_tok.type = TOK_INT;
            pos = 0;
            while (isdigit(peek()) && pos < TOK_BUF_SIZE - 1) {
                cur_tok.text[pos++] = advance();
            }
            cur_tok.text[pos] = 0;
            cur_tok.int_val = 0;
            int i;
            for (i = 0; i < pos; i++)
                cur_tok.int_val = cur_tok.int_val * 10 + (cur_tok.text[i] - '0');
            cur_tok.int_val = -cur_tok.int_val;
            return;
        }
        /* Otherwise '-' is start of operator */
        cur_tok.type = TOK_ATOM;
        cur_tok.text[0] = '-';
        pos = 1;
        while (is_op_char(peek()) && pos < TOK_BUF_SIZE - 1) {
            cur_tok.text[pos++] = advance();
        }
        cur_tok.text[pos] = 0;
        return;
    }

    /* Operator characters */
    if (is_op_char(c)) {
        cur_tok.type = TOK_ATOM;
        pos = 0;
        while (is_op_char(peek()) && pos < TOK_BUF_SIZE - 1) {
            cur_tok.text[pos++] = advance();
        }
        cur_tok.text[pos] = 0;
        return;
    }

    /* Unknown character - skip */
    advance();
    goto retry;
}
