#ifndef SBASIC_LEXER_H
#define SBASIC_LEXER_H

typedef enum {
    /* Literals */
    TOK_INTEGER_LIT,    /* 42 */
    TOK_DOUBLE_LIT,     /* 3.14 */
    TOK_STRING_LIT,     /* "hello" */
    TOK_IDENT,          /* variable or function name (with suffix) */

    /* Operators */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH,
    TOK_BACKSLASH,      /* \ integer division */
    TOK_CARET,          /* ^ power */
    TOK_EQ, TOK_NE,     /* = <> */
    TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_LPAREN, TOK_RPAREN,
    TOK_COMMA, TOK_SEMICOLON, TOK_COLON,

    /* Keywords */
    TOK_PRINT, TOK_INPUT, TOK_LET,
    TOK_CLS, TOK_LOCATE, TOK_COLOR,
    TOK_IF, TOK_THEN, TOK_ELSE, TOK_END,
    TOK_FOR, TOK_TO, TOK_STEP, TOK_NEXT,
    TOK_WHILE, TOK_WEND,
    TOK_AND, TOK_OR, TOK_NOT, TOK_MOD,
    TOK_REM,
    TOK_DIM,

    /* Stage 2 keywords */
    TOK_SUB, TOK_FUNCTION, TOK_CALL,
    TOK_GOTO, TOK_GOSUB, TOK_RETURN,
    TOK_DO, TOK_LOOP, TOK_UNTIL,
    TOK_SELECT, TOK_CASE,
    TOK_EXIT,
    TOK_CONST, TOK_SHARED, TOK_DECLARE,
    TOK_AS, TOK_IS,

    /* Stage 3 keywords */
    TOK_DATA, TOK_READ, TOK_RESTORE,
    TOK_ERASE, TOK_REDIM,

    /* Stage 4 keywords */
    TOK_SWAP, TOK_RANDOMIZE, TOK_USING, TOK_SLEEP,

    /* Stage 5 keywords */
    TOK_OPEN, TOK_CLOSE, TOK_LINE, TOK_WRITE,
    TOK_KILL, TOK_NAME, TOK_HASH,
    TOK_APPEND, TOK_OUTPUT,
    TOK_BINARY, TOK_RANDOM, TOK_GET, TOK_PUT, TOK_SEEK,

    /* Stage 6 keywords */
    TOK_TYPE, TOK_STATIC, TOK_ON, TOK_ERROR,
    TOK_RESUME, TOK_DEFINT, TOK_DEFDBL, TOK_DEFSTR, TOK_DEFSNG,
    TOK_DOT,

    /* Extra keywords */
    TOK_BEEP, TOK_TRACE, TOK_SPLIT, TOK_XOR, TOK_IMP, TOK_EQV, TOK_ELSEIF,
    TOK_AMPERSAND,
    TOK_DEF,
    TOK_SHELL,
    TOK_CHDIR, TOK_MKDIR, TOK_RMDIR,
    TOK_LPRINT,
    TOK_POKE, TOK_SCREEN,
    TOK_FIELD, TOK_LSET, TOK_RSET,
    TOK_WIDTH, TOK_CHAIN, TOK_PCOPY,
    TOK_KEY, TOK_SOUND, TOK_PLAY,
    TOK_CLEAR, TOK_ENVIRON, TOK_VIEW,
    TOK_PALETTE, TOK_WAIT, TOK_OUT,
    TOK_COMMON,

    /* REPL commands */
    TOK_RUN, TOK_LIST, TOK_NEW, TOK_LOAD, TOK_SAVE, TOK_BYE,

    /* Control */
    TOK_EOL,            /* end of line */
    TOK_EOF,            /* end of input */
} token_type_t;

typedef struct {
    token_type_t type;
    int line;
    /* For literals/identifiers */
    union {
        int ival;
        double dval;
    };
    char text[256];     /* original text (for idents, strings) */
    char suffix;        /* type suffix: '%', '#', '$', or '\0' */
} token_t;

typedef struct {
    const char *src;    /* source text */
    int pos;            /* current position */
    int line;           /* current line number */
    token_t cur;        /* current (peeked) token */
    int has_cur;        /* whether cur is valid */
} lexer_t;

/* Initialize lexer with source text */
void lexer_init(lexer_t *lex, const char *src, int start_line);

/* Peek at current token without consuming */
token_t *lexer_peek(lexer_t *lex);

/* Consume and return current token */
token_t lexer_next(lexer_t *lex);

/* Check if current token matches type (without consuming) */
int lexer_check(lexer_t *lex, token_type_t type);

/* Consume token if it matches type, return 1; else return 0 */
int lexer_match(lexer_t *lex, token_type_t type);

/* Return the keyword name for a token type (or NULL) */
const char *token_type_name(token_type_t type);

#endif
