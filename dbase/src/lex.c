#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "lex.h"
#include "util.h"
#include "date.h"

void lexer_init(lexer_t *l, const char *input) {
    lexer_init_ext(l, input, NULL);
}

void lexer_init_ext(lexer_t *l, const char *input, memvar_store_t *store) {
    memset(l, 0, sizeof(*l));
    l->input = input;
    l->p = input;
    l->store = store;
    l->error = NULL;
    lex_next(l); /* prime the first token */
}

/* Internal helper: get next char, handling macro expansion stack */
static char lex_get_char(lexer_t *l);
static char lex_peek_char(lexer_t *l);
static void lex_unget_char(lexer_t *l, char c);
static int lex_push_macro(lexer_t *l, const char *text);
static void macro_expand_into(const char *src, memvar_store_t *store,
                              char *dst, int size, int depth);

void lex_get_remaining(lexer_t *l, char *out_buf, int size) {
    const char *start = l->token_start ? l->token_start : "";
    if (l->store) {
        macro_expand_into(start, l->store, out_buf, size, 0);
    } else {
        str_copy(out_buf, start, size);
    }
    l->current.type = TOK_EOF;
}

static char lex_get_char(lexer_t *l) {
    for (;;) {
        char c;

        while (l->macro_depth > 0) {
            c = *l->macro_stack[l->macro_depth - 1].p;
            if (c != '\0') {
                l->macro_stack[l->macro_depth - 1].p++;
                break;
            }
            /* End of current macro level */
            l->macro_depth--;
        }
        if (l->macro_depth == 0) {
            c = *l->p;
            if (c != '\0') l->p++;
        }

        if (c == '\0') return '\0';

        /* Macro expansion: &name -> memvar string (supports nesting) */
        if (c == '&' && l->store && lex_peek_char(l) != '&') {
            char name[MEMVAR_NAMELEN];
            value_t val;
            int ni = 0;
            char n = lex_peek_char(l);

            if (!is_ident_start(n)) {
                return c;
            }

            while (is_ident_char(n = lex_get_char(l)) && ni < MEMVAR_NAMELEN - 1) {
                name[ni++] = n;
            }
            name[ni] = '\0';

            if (n != '.') {
                lex_unget_char(l, n);
            }

            if (memvar_find(l->store, name, &val) == 0 && val.type == VAL_CHAR) {
                if (lex_push_macro(l, val.str) < 0) {
                    l->error = "Macro nesting overflow";
                    return '\0';
                }
                continue; /* pull from expanded macro */
            }
            /* If not found or not string, expand to nothing */
            continue;
        }

        return c;
    }
}

/* Internal helper: peek next char without consuming */
static char lex_peek_char(lexer_t *l) {
    if (l->macro_depth > 0) {
        return *l->macro_stack[l->macro_depth - 1].p;
    }
    return *l->p;
}

/* Internal helper: "unget" char (only works within current level) */
static void lex_unget_char(lexer_t *l, char c) {
    if (c == '\0') return;
    if (l->macro_depth > 0) {
        if (l->macro_stack[l->macro_depth - 1].p > l->macro_stack[l->macro_depth - 1].buf)
            l->macro_stack[l->macro_depth - 1].p--;
    } else {
        if (l->p > l->input) l->p--;
    }
}

static int lex_push_macro(lexer_t *l, const char *text) {
    if (!text || text[0] == '\0') return 0;
    if (l->macro_depth >= MAX_MACRO_NESTING) return -1;
    str_copy(l->macro_stack[l->macro_depth].buf, text,
             sizeof(l->macro_stack[l->macro_depth].buf));
    l->macro_stack[l->macro_depth].p = l->macro_stack[l->macro_depth].buf;
    l->macro_depth++;
    return 0;
}

static void macro_expand_into(const char *src, memvar_store_t *store,
                              char *dst, int size, int depth) {
    const char *s = src;
    char *d = dst;
    char *end = dst + size - 1;

    if (!store || depth >= MAX_MACRO_NESTING) {
        str_copy(dst, src, size);
        return;
    }

    while (*s && d < end) {
        if (*s == '&' && s[1] != '&' && is_ident_start(s[1])) {
            char name[MEMVAR_NAMELEN];
            value_t val;
            int i = 0;
            s++; /* skip & */
            while (is_ident_char(*s) && i < MEMVAR_NAMELEN - 1)
                name[i++] = *s++;
            name[i] = '\0';
            if (*s == '.') s++; /* optional delimiter */

            if (memvar_find(store, name, &val) == 0 && val.type == VAL_CHAR) {
                char expanded[256];
                macro_expand_into(val.str, store, expanded, sizeof(expanded), depth + 1);
                {
                    int len = strlen(expanded);
                    if (d + len > end) len = (int)(end - d);
                    memcpy(d, expanded, len);
                    d += len;
                }
            }
            /* If not found or not string, substitute nothing */
            continue;
        }
        *d++ = *s++;
    }
    *d = '\0';
}
token_type_t lex_next(lexer_t *l) {
    char c;
    token_t *t = &l->current;
    token_type_t prev = t->type;
    l->prev_type = prev;

    /* Skip whitespace */
    for (;;) {
        c = lex_get_char(l);
        if (c == '\0') {
            l->token_start = l->p;
            t->type = TOK_EOF;
            t->text[0] = '\0';
            return t->type;
        }
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
    }

    /* NOTE: When inside a macro expansion, token_start points into the macro
       stack buffer, not the original input.  Callers that capture token_start
       (e.g., FOR/WHILE condition strings) must memcpy immediately before the
       lexer advances or the stack is reused. */
    l->token_start = (l->macro_depth > 0) ? l->macro_stack[l->macro_depth - 1].p - 1 : l->p - 1;

    t->text[0] = c;
    t->text[1] = '\0';

    /* Identifiers and potential Keywords */
    if (is_ident_start(c)) {
        int i = 0;
        t->text[i++] = c;
        while (is_ident_char(c = lex_get_char(l)) && i < (int)sizeof(t->text) - 1) {
            t->text[i++] = c;
        }
        t->text[i] = '\0';
        lex_unget_char(l, c);
        t->type = TOK_IDENT;
        return t->type;
    }

    /* Numbers */
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)lex_peek_char(l)))) {
        int i = 0;
        t->text[i++] = c;
        while ((isdigit((unsigned char)(c = lex_get_char(l))) || c == '.') && i < (int)sizeof(t->text) - 1) {
            t->text[i++] = c;
        }
        t->text[i] = '\0';
        lex_unget_char(l, c);
        t->num_val = atof(t->text);
        t->type = TOK_NUMBER;
        return t->type;
    }

    /* String literals */
    if (c == '"' || c == '\'') {
        char quote = c;
        int i = 0;
        while ((c = lex_get_char(l)) != '\0' && c != quote && i < (int)sizeof(t->text) - 1) {
            t->text[i++] = c;
        }
        t->text[i] = '\0';
        t->type = TOK_STRING;
        return t->type;
    }

    /* Logical literals or Dot Keywords */
    if (c == '.') {
        char buf[32];
        int i = 0;
        while (is_ident_char(c = lex_get_char(l)) && i < (int)sizeof(buf) - 1) {
            buf[i++] = c;
        }
        buf[i] = '\0';
        if (c == '.') {
            if (str_icmp(buf, "AND") == 0) { t->type = TOK_AND; return t->type; }
            if (str_icmp(buf, "OR") == 0) { t->type = TOK_OR; return t->type; }
            if (str_icmp(buf, "NOT") == 0) { t->type = TOK_NOT; return t->type; }
            if (str_icmp(buf, "T") == 0) { t->type = TOK_LOGIC; t->logic_val = 1; return t->type; }
            if (str_icmp(buf, "F") == 0) { t->type = TOK_LOGIC; t->logic_val = 0; return t->type; }
        }
        /* Backtrack if not a dot keyword */
        while (i > 0) lex_unget_char(l, buf[--i]);
        lex_unget_char(l, c);
        c = '.'; /* restore c */
    }

    /* Date literals {MM/DD/YY} */
    if (c == '{') {
        char buf[64];
        int i = 0;
        while ((c = lex_get_char(l)) != '\0' && c != '}' && i < (int)sizeof(buf) - 1) {
            buf[i++] = c;
        }
        if (c == '}') {
            buf[i] = '\0';
            t->date_val = date_from_mdy(buf);
            t->type = TOK_DATE;
            return t->type;
        }
        /* backtrack if not a proper date literal */
        while (i > 0) lex_unget_char(l, buf[--i]);
        lex_unget_char(l, c);
        c = '{';
    }

    /* Multi-character operators */
    char next = lex_get_char(l);
    if (c == '<' && next == '>') { t->type = TOK_NE; return t->type; }
    if (c == '<' && next == '=') { t->type = TOK_LE; return t->type; }
    if (c == '>' && next == '=') { t->type = TOK_GE; return t->type; }
    if (c == '-' && next == '>') { t->type = TOK_ARROW; return t->type; }
    if (c == '*' && next == '*') { t->type = TOK_POWER; return t->type; }
    if (c == '=' && next == '=') { t->type = TOK_EXACT_EQ; return t->type; }
    lex_unget_char(l, next);

    /* Single character operators */
    switch (c) {
        case '+': t->type = TOK_PLUS; break;
        case '-': t->type = TOK_MINUS; break;
        case '*': t->type = TOK_MUL; break;
        case '/': t->type = TOK_DIV; break;
        case '(': t->type = TOK_LPAREN; break;
        case ')': t->type = TOK_RPAREN; break;
        case '[':
            /* After identifier/rparen/rbracket: array index. Otherwise: string literal. */
            if (prev == TOK_IDENT || prev == TOK_RPAREN || prev == TOK_RBRACKET) {
                t->type = TOK_LBRACKET;
            } else {
                int i = 0;
                while ((c = lex_get_char(l)) != '\0' && c != ']' && i < (int)sizeof(t->text) - 1)
                    t->text[i++] = c;
                t->text[i] = '\0';
                t->type = TOK_STRING;
            }
            break;
        case ']': t->type = TOK_RBRACKET; break;
        case ',': t->type = TOK_COMMA; break;
        case '=': t->type = TOK_EQ; break;
        case '<': t->type = TOK_LT; break;
        case '>': t->type = TOK_GT; break;
        case '#': t->type = TOK_NE; break;
        case '$': t->type = TOK_SUBSTR; break;
        case '&': t->type = TOK_MACRO; break;
        case '^': t->type = TOK_POWER; break;
        case '!': t->type = TOK_NOT; break;
        case '.': t->type = TOK_DOT; break;
        default:
            t->type = TOK_ERROR;
            l->error = "Unexpected character";
            break;
    }
    
    return t->type;
}

token_type_t lex_peek(lexer_t *l) {
    return l->current.type;
}

int is_keyword(const char *ident, const char *kw) {
    int ident_len = strlen(ident);
    int kw_len = strlen(kw);
    int min_len = (kw_len < 4) ? kw_len : 4;
    if (ident_len < min_len) return 0;
    if (ident_len > kw_len) return 0;
    return str_nicmp(ident, kw, ident_len) == 0;
}

int lex_is_reserved(const char *ident) {
    static const char *reserved[] = {
        "PROCEDURE", "FUNCTION", "IF", "ELSE", "ENDIF",
        "DO", "WHILE", "ENDDO", "FOR", "NEXT", "RETURN",
        "PARAMETERS", "PRIVATE", "PUBLIC", "CASE", "OTHERWISE",
        "ENDCASE", "LOOP", "EXIT", "QUIT", "CANCEL", NULL
    };
    int i;
    for (i = 0; reserved[i]; i++) {
        if (is_keyword(ident, reserved[i])) return 1;
    }
    return 0;
}
