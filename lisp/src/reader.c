#include "reader.h"
#include "heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int peek_ch;
static int has_peek;

void reader_init(void) {
    has_peek = 0;
    peek_ch = 0;
}

static int read_char(void) {
    if (has_peek) {
        has_peek = 0;
        return peek_ch;
    }
    return getchar();
}

static int peek(void) {
    if (!has_peek) {
        peek_ch = getchar();
        has_peek = 1;
    }
    return peek_ch;
}

static void skip_whitespace_and_comments(void) {
    for (;;) {
        int c = peek();
        if (c < 0) return;
        if (c == ';') {
            /* comment to end of line */
            while (c >= 0 && c != '\n') {
                read_char();
                c = peek();
            }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            read_char();
            continue;
        }
        return;
    }
}

static int is_symbol_char(int c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '-' || c == '_' || c == '!' || c == '?' || c == '*') return 1;
    if (c == '+' || c == '/' || c == '<' || c == '>' || c == '=') return 1;
    return 0;
}

/* (read_number removed - read_symbol_or_number handles all number parsing) */

static val_t read_string(void) {
    int cap = 256;
    int len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { lisp_error("out of memory"); return NIL; }
    for (;;) {
        int c = read_char();
        if (c < 0 || c == '"') break;
        if (c == '\\') {
            c = read_char();
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == '\\') c = '\\';
            else if (c == '"') c = '"';
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); lisp_error("out of memory"); return NIL; }
            buf = nb;
        }
        buf[len++] = c;
    }
    buf[len] = 0;
    val_t result = string_alloc(buf, len);
    free(buf);
    return result;
}

static val_t read_symbol_or_number(int first) {
    char buf[256];
    int len = 0;
    buf[len++] = first;
    while (is_symbol_char(peek())) {
        if (len >= 255) {
            lisp_error("symbol too long");
            return NIL;
        }
        buf[len++] = read_char();
    }
    buf[len] = 0;

    /* Check if it's a number: optional minus followed by digits */
    int i = 0;
    int is_num = 1;
    if (buf[0] == '-' || buf[0] == '+') {
        if (len == 1) is_num = 0; /* bare - or + is a symbol */
        i = 1;
    }
    while (i < len) {
        if (buf[i] < '0' || buf[i] > '9') { is_num = 0; break; }
        i++;
    }
    if (is_num && len > 0 && !(len == 1 && (buf[0] == '-' || buf[0] == '+'))) {
        long val = 0;
        int neg = 0;
        int j = 0;
        if (buf[0] == '-') { neg = 1; j = 1; }
        else if (buf[0] == '+') { j = 1; }
        while (j < len) {
            val = val * 10 + (buf[j] - '0');
            j++;
        }
        if (neg) val = -val;
        return FIXNUM((int)val);
    }

    return symbol_intern(buf);
}

static val_t lisp_read_expr(int *eof);

static val_t read_list(void) {
    val_t head = NIL;
    val_t tail = NIL;
    PUSH_ROOT(head);
    PUSH_ROOT(tail);

    for (;;) {
        skip_whitespace_and_comments();
        int c = peek();
        if (c < 0) {
            lisp_error("unexpected EOF in list");
            POP_ROOTS(2);
            return NIL;
        }
        if (c == ')') {
            read_char();
            POP_ROOTS(2);
            return head;
        }
        /* dotted pair */
        if (c == '.') {
            read_char();
            /* Check it's actually a dot separator, not a symbol starting with . */
            int next = peek();
            if (next == ' ' || next == '\t' || next == '\n' || next == '\r' ||
                next == ')' || next == '(') {
                int eof = 0;
                val_t rest = lisp_read_expr(&eof);
                if (g_error) { POP_ROOTS(2); return NIL; }
                if (!IS_NIL(tail)) {
                    CDR(tail) = rest;
                } else {
                    head = rest;
                }
                skip_whitespace_and_comments();
                if (peek() == ')') read_char();
                POP_ROOTS(2);
                return head;
            }
            /* It was a symbol starting with '.', put it back conceptually */
            /* Actually '.' is not a valid symbol char, so this shouldn't happen */
            lisp_error("unexpected '.'");
            POP_ROOTS(2);
            return NIL;
        }

        int eof = 0;
        val_t elem = lisp_read_expr(&eof);
        if (g_error) { POP_ROOTS(2); return NIL; }
        PUSH_ROOT(elem);
        val_t cell = cons_alloc(elem, NIL);
        POP_ROOTS(1); /* elem */
        if (IS_NIL(head)) {
            head = cell;
            tail = cell;
        } else {
            CDR(tail) = cell;
            tail = cell;
        }
    }
}

static val_t lisp_read_expr(int *eof) {
    skip_whitespace_and_comments();
    int c = peek();
    if (c < 0) {
        *eof = 1;
        return NIL;
    }

    c = read_char();

    if (c == '(') {
        return read_list();
    }

    if (c == '\'') {
        val_t quoted = NIL;
        PUSH_ROOT(quoted);
        int e = 0;
        quoted = lisp_read_expr(&e);
        if (g_error) { POP_ROOTS(1); return NIL; }
        val_t sym_quote = symbol_intern("quote");
        PUSH_ROOT(sym_quote);
        val_t inner = cons_alloc(quoted, NIL);
        PUSH_ROOT(inner);
        val_t result = cons_alloc(sym_quote, inner);
        POP_ROOTS(3);
        return result;
    }

    if (c == '"') {
        return read_string();
    }

    if (c == '#') {
        int next = read_char();
        if (next == 't') {
            /* Consume any trailing symbol chars like #true */
            while (is_symbol_char(peek())) read_char();
            return sym_true;
        }
        if (next == 'f') {
            while (is_symbol_char(peek())) read_char();
            return NIL; /* #f = NIL */
        }
        lisp_error("unknown # syntax");
        return NIL;
    }

    if (is_symbol_char(c)) {
        return read_symbol_or_number(c);
    }

    lisp_error("unexpected character");
    return NIL;
}

val_t lisp_read(int *eof) {
    *eof = 0;
    return lisp_read_expr(eof);
}
