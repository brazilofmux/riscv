#include "lexer.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* Keyword table: uppercase name -> token type */
typedef struct {
    const char *name;
    token_type_t type;
} keyword_t;

static const keyword_t keywords[] = {
    { "PRINT",  TOK_PRINT },
    { "INPUT",  TOK_INPUT },
    { "LET",    TOK_LET },
    { "CLS",    TOK_CLS },
    { "LOCATE", TOK_LOCATE },
    { "COLOR",  TOK_COLOR },
    { "IF",     TOK_IF },
    { "THEN",   TOK_THEN },
    { "ELSE",   TOK_ELSE },
    { "END",    TOK_END },
    { "FOR",    TOK_FOR },
    { "TO",     TOK_TO },
    { "STEP",   TOK_STEP },
    { "NEXT",   TOK_NEXT },
    { "WHILE",  TOK_WHILE },
    { "WEND",   TOK_WEND },
    { "AND",    TOK_AND },
    { "OR",     TOK_OR },
    { "NOT",    TOK_NOT },
    { "MOD",    TOK_MOD },
    { "REM",    TOK_REM },
    { "DIM",    TOK_DIM },
    { "SUB",    TOK_SUB },
    { "FUNCTION", TOK_FUNCTION },
    { "CALL",   TOK_CALL },
    { "GOTO",   TOK_GOTO },
    { "GOSUB",  TOK_GOSUB },
    { "RETURN", TOK_RETURN },
    { "DO",     TOK_DO },
    { "LOOP",   TOK_LOOP },
    { "UNTIL",  TOK_UNTIL },
    { "SELECT", TOK_SELECT },
    { "CASE",   TOK_CASE },
    { "EXIT",   TOK_EXIT },
    { "CONST",  TOK_CONST },
    { "SHARED", TOK_SHARED },
    { "DECLARE", TOK_DECLARE },
    { "AS",     TOK_AS },
    { "IS",     TOK_IS },
    { "DATA",   TOK_DATA },
    { "READ",   TOK_READ },
    { "RESTORE", TOK_RESTORE },
    { "ERASE",  TOK_ERASE },
    { "REDIM",  TOK_REDIM },
    { "SWAP",   TOK_SWAP },
    { "RANDOMIZE", TOK_RANDOMIZE },
    { "USING",  TOK_USING },
    { "SLEEP",  TOK_SLEEP },
    { "OPEN",   TOK_OPEN },
    { "CLOSE",  TOK_CLOSE },
    { "LINE",   TOK_LINE },
    { "WRITE",  TOK_WRITE },
    { "KILL",   TOK_KILL },
    { "NAME",   TOK_NAME },
    { "APPEND", TOK_APPEND },
    { "OUTPUT", TOK_OUTPUT },
    { "BINARY", TOK_BINARY },
    { "RANDOM", TOK_RANDOM },
    { "GET",    TOK_GET },
    { "PUT",    TOK_PUT },
    { "SEEK",   TOK_SEEK },
    { "TYPE",   TOK_TYPE },
    { "STATIC", TOK_STATIC },
    { "ON",     TOK_ON },
    { "ERROR",  TOK_ERROR },
    { "RESUME", TOK_RESUME },
    { "DEFINT", TOK_DEFINT },
    { "DEFDBL", TOK_DEFDBL },
    { "DEFSTR", TOK_DEFSTR },
    { "DEFSNG", TOK_DEFSNG },
    { "BEEP",   TOK_BEEP },
    { "TRACE",  TOK_TRACE },
    { "SPLIT",  TOK_SPLIT },
    { "XOR",    TOK_XOR },
    { "IMP",    TOK_IMP },
    { "EQV",    TOK_EQV },
    { "ELSEIF", TOK_ELSEIF },
    { "DEF",    TOK_DEF },
    { "SHELL",  TOK_SHELL },
    { "CHDIR",  TOK_CHDIR },
    { "MKDIR",  TOK_MKDIR },
    { "RMDIR",  TOK_RMDIR },
    { "LPRINT", TOK_LPRINT },
    { "POKE",   TOK_POKE },
    { "SCREEN", TOK_SCREEN },
    { "FIELD",  TOK_FIELD },
    { "LSET",   TOK_LSET },
    { "RSET",   TOK_RSET },
    { "WIDTH",  TOK_WIDTH },
    { "CHAIN",  TOK_CHAIN },
    { "PCOPY",  TOK_PCOPY },
    { "KEY",    TOK_KEY },
    { "SOUND",  TOK_SOUND },
    { "PLAY",   TOK_PLAY },
    { "CLEAR",  TOK_CLEAR },
    { "ENVIRON", TOK_ENVIRON },
    { "VIEW",   TOK_VIEW },
    { "PALETTE", TOK_PALETTE },
    { "WAIT",   TOK_WAIT },
    { "OUT",    TOK_OUT },
    { "COMMON", TOK_COMMON },
    { "RUN",    TOK_RUN },
    { "LIST",   TOK_LIST },
    { "NEW",    TOK_NEW },
    { "LOAD",   TOK_LOAD },
    { "SAVE",   TOK_SAVE },
    { "BYE",    TOK_BYE },
    { NULL, 0 }
};

static token_type_t lookup_keyword(const char *upper) {
    for (int i = 0; keywords[i].name; i++) {
        if (strcmp(upper, keywords[i].name) == 0)
            return keywords[i].type;
    }
    return TOK_IDENT;
}

const char *token_type_name(token_type_t type) {
    for (int i = 0; keywords[i].name; i++) {
        if (keywords[i].type == type)
            return keywords[i].name;
    }
    switch (type) {
        case TOK_INTEGER_LIT: return "integer";
        case TOK_DOUBLE_LIT:  return "double";
        case TOK_STRING_LIT:  return "string";
        case TOK_IDENT:       return "identifier";
        case TOK_PLUS:  return "+";
        case TOK_MINUS: return "-";
        case TOK_STAR:  return "*";
        case TOK_SLASH: return "/";
        case TOK_BACKSLASH: return "\\";
        case TOK_CARET: return "^";
        case TOK_EQ:    return "=";
        case TOK_NE:    return "<>";
        case TOK_LT:    return "<";
        case TOK_GT:    return ">";
        case TOK_LE:    return "<=";
        case TOK_GE:    return ">=";
        case TOK_LPAREN:    return "(";
        case TOK_RPAREN:    return ")";
        case TOK_COMMA:     return ",";
        case TOK_SEMICOLON: return ";";
        case TOK_COLON:     return ":";
        case TOK_EOL:   return "end of line";
        case TOK_EOF:   return "end of file";
        default:        return "?";
    }
}

void lexer_init(lexer_t *lex, const char *src, int start_line) {
    lex->src = src;
    lex->pos = 0;
    lex->line = start_line;
    lex->has_cur = 0;
}

static char peek_char(lexer_t *lex) {
    return lex->src[lex->pos];
}

static char next_char(lexer_t *lex) {
    char c = lex->src[lex->pos];
    if (c) lex->pos++;
    return c;
}

static void skip_spaces(lexer_t *lex) {
    while (lex->src[lex->pos] == ' ' || lex->src[lex->pos] == '\t')
        lex->pos++;
}

static token_t scan_token(lexer_t *lex) {
    token_t tok;
    memset(&tok, 0, sizeof(tok));
    tok.line = lex->line;

    skip_spaces(lex);

    char c = peek_char(lex);

    /* End of string */
    if (c == '\0') {
        tok.type = TOK_EOF;
        return tok;
    }

    /* Newline */
    if (c == '\n') {
        next_char(lex);
        lex->line++;
        tok.type = TOK_EOL;
        return tok;
    }
    if (c == '\r') {
        next_char(lex);
        if (peek_char(lex) == '\n') next_char(lex);
        lex->line++;
        tok.type = TOK_EOL;
        return tok;
    }

    /* Comment: ' */
    if (c == '\'') {
        /* Skip to end of line */
        while (peek_char(lex) && peek_char(lex) != '\n' && peek_char(lex) != '\r')
            next_char(lex);
        tok.type = TOK_EOL;
        /* Don't consume the newline — let the next scan get it.
           But we return EOL to end this logical line. */
        return tok;
    }

    /* String literal */
    if (c == '"') {
        next_char(lex); /* consume opening quote */
        int len = 0;
        while (peek_char(lex) && peek_char(lex) != '"' && peek_char(lex) != '\n') {
            if (len < 255)
                tok.text[len++] = next_char(lex);
            else
                next_char(lex);
        }
        tok.text[len] = '\0';
        if (peek_char(lex) == '"')
            next_char(lex); /* consume closing quote */
        tok.type = TOK_STRING_LIT;
        return tok;
    }

    /* Hex/Octal/Binary literal: &HFF, &O77, &B1010 */
    if (c == '&') {
        next_char(lex); /* consume '&' */
        char prefix = toupper(peek_char(lex));
        int base = 0;
        if (prefix == 'H') { base = 16; next_char(lex); }
        else if (prefix == 'O') { base = 8; next_char(lex); }
        else if (prefix == 'B') { base = 2; next_char(lex); }

        if (base > 0) {
            int len = 0;
            while (1) {
                char d = peek_char(lex);
                int valid = 0;
                if (base == 16) valid = isxdigit(d);
                else if (base == 8) valid = (d >= '0' && d <= '7');
                else valid = (d == '0' || d == '1');
                if (!valid) break;
                if (len < 255) tok.text[len++] = next_char(lex);
                else next_char(lex);
            }
            tok.text[len] = '\0';
            /* Optional trailing % suffix (already integer) */
            if (peek_char(lex) == '%') next_char(lex);
            tok.type = TOK_INTEGER_LIT;
            tok.ival = (len > 0) ? (int)strtol(tok.text, NULL, base) : 0;
            return tok;
        }
        /* Bare '&' → string concatenation operator */
        tok.type = TOK_AMPERSAND;
        return tok;
    }

    /* Number */
    if (isdigit(c) || (c == '.' && isdigit(lex->src[lex->pos + 1]))) {
        int len = 0;
        int is_double = 0;
        while (isdigit(peek_char(lex))) {
            if (len < 255) tok.text[len++] = next_char(lex);
            else next_char(lex);
        }
        if (peek_char(lex) == '.' && isdigit(lex->src[lex->pos + 1])) {
            is_double = 1;
            if (len < 255) tok.text[len++] = next_char(lex);
            else next_char(lex);
            while (isdigit(peek_char(lex))) {
                if (len < 255) tok.text[len++] = next_char(lex);
                else next_char(lex);
            }
        }
        /* Exponent */
        if (peek_char(lex) == 'e' || peek_char(lex) == 'E') {
            is_double = 1;
            if (len < 255) tok.text[len++] = next_char(lex);
            else next_char(lex);
            if (peek_char(lex) == '+' || peek_char(lex) == '-') {
                if (len < 255) tok.text[len++] = next_char(lex);
                else next_char(lex);
            }
            while (isdigit(peek_char(lex))) {
                if (len < 255) tok.text[len++] = next_char(lex);
                else next_char(lex);
            }
        }
        tok.text[len] = '\0';
        /* Type suffix on number */
        if (peek_char(lex) == '%') {
            next_char(lex);
            is_double = 0;
        } else if (peek_char(lex) == '#' || peek_char(lex) == '!') {
            next_char(lex);
            is_double = 1;
        }
        if (is_double) {
            tok.type = TOK_DOUBLE_LIT;
            tok.dval = atof(tok.text);
        } else {
            tok.type = TOK_INTEGER_LIT;
            tok.ival = atoi(tok.text);
        }
        return tok;
    }

    /* Line continuation: _ followed by optional spaces and newline */
    if (c == '_') {
        int save_pos = lex->pos;
        next_char(lex); /* consume _ */
        /* Check if only whitespace until newline */
        while (peek_char(lex) == ' ' || peek_char(lex) == '\t')
            next_char(lex);
        if (peek_char(lex) == '\n' || peek_char(lex) == '\r') {
            /* Line continuation — skip the newline and continue scanning */
            if (peek_char(lex) == '\r') next_char(lex);
            if (peek_char(lex) == '\n') next_char(lex);
            lex->line++;
            return scan_token(lex); /* recurse to get next real token */
        }
        /* Not a continuation — restore and fall through to identifier */
        lex->pos = save_pos;
    }

    /* Identifier or keyword */
    if (isalpha(c) || c == '_') {
        int len = 0;
        char upper[256];
        while (isalnum(peek_char(lex)) || peek_char(lex) == '_') {
            char ch = next_char(lex);
            if (len < 255) {
                tok.text[len] = ch;
                upper[len] = toupper(ch);
                len++;
            }
        }
        tok.text[len] = '\0';
        upper[len] = '\0';

        /* Check for type suffix */
        if (peek_char(lex) == '$' || peek_char(lex) == '%' ||
            peek_char(lex) == '#' || peek_char(lex) == '!') {
            tok.suffix = next_char(lex);
            /* Treat ! (single) same as # (double) since we only have double */
            if (tok.suffix == '!') tok.suffix = '#';
            tok.text[len] = tok.suffix;
            tok.text[len + 1] = '\0';
        }

        /* Check for keyword (only if no suffix) */
        if (!tok.suffix) {
            token_type_t kw = lookup_keyword(upper);
            if (kw != TOK_IDENT) {
                tok.type = kw;
                /* REM: skip rest of line */
                if (kw == TOK_REM) {
                    while (peek_char(lex) && peek_char(lex) != '\n' && peek_char(lex) != '\r')
                        next_char(lex);
                    tok.type = TOK_EOL;
                }
                return tok;
            }
        }

        /* Store upper-cased name for case-insensitive lookup */
        strcpy(tok.text, upper);
        if (tok.suffix) {
            int ulen = (int)strlen(tok.text);
            tok.text[ulen] = tok.suffix;
            tok.text[ulen + 1] = '\0';
        }

        tok.type = TOK_IDENT;
        return tok;
    }

    /* Operators */
    next_char(lex);
    switch (c) {
        case '+': tok.type = TOK_PLUS; break;
        case '-': tok.type = TOK_MINUS; break;
        case '*': tok.type = TOK_STAR; break;
        case '/': tok.type = TOK_SLASH; break;
        case '\\': tok.type = TOK_BACKSLASH; break;
        case '^': tok.type = TOK_CARET; break;
        case '(': tok.type = TOK_LPAREN; break;
        case ')': tok.type = TOK_RPAREN; break;
        case ',': tok.type = TOK_COMMA; break;
        case ';': tok.type = TOK_SEMICOLON; break;
        case ':': tok.type = TOK_COLON; break;
        case '?': tok.type = TOK_PRINT; break;
        case '=': tok.type = TOK_EQ; break;
        case '#': tok.type = TOK_HASH; break;
        case '.':
            /* Dot must not be followed by a digit (that's a number) */
            tok.type = TOK_DOT; break;
        case '<':
            if (peek_char(lex) == '>') { next_char(lex); tok.type = TOK_NE; }
            else if (peek_char(lex) == '=') { next_char(lex); tok.type = TOK_LE; }
            else tok.type = TOK_LT;
            break;
        case '>':
            if (peek_char(lex) == '=') { next_char(lex); tok.type = TOK_GE; }
            else tok.type = TOK_GT;
            break;
        default:
            /* Unknown character — treat as EOL to skip */
            tok.type = TOK_EOL;
            break;
    }
    return tok;
}

token_t *lexer_peek(lexer_t *lex) {
    if (!lex->has_cur) {
        lex->cur = scan_token(lex);
        lex->has_cur = 1;
    }
    return &lex->cur;
}

token_t lexer_next(lexer_t *lex) {
    if (!lex->has_cur)
        lex->cur = scan_token(lex);
    lex->has_cur = 0;
    return lex->cur;
}

int lexer_check(lexer_t *lex, token_type_t type) {
    return lexer_peek(lex)->type == type;
}

int lexer_match(lexer_t *lex, token_type_t type) {
    if (lexer_check(lex, type)) {
        lexer_next(lex);
        return 1;
    }
    return 0;
}
