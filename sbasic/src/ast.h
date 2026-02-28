#ifndef SBASIC_AST_H
#define SBASIC_AST_H

#include "value.h"

/* Expression node types */
typedef enum {
    EXPR_LITERAL,       /* integer, double, or string constant */
    EXPR_VARIABLE,      /* variable reference */
    EXPR_UNARY,         /* unary op: -expr, NOT expr */
    EXPR_BINARY,        /* binary op: a + b, a AND b, etc. */
    EXPR_COMPARE,       /* comparison: a = b, a < b, etc. */
    EXPR_CALL,          /* built-in or user function call: ABS(x), MyFunc(x) */
    EXPR_FIELD_ACCESS,  /* var.field */
} expr_type_t;

/* Binary operators */
typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_IDIV, OP_MOD, OP_POW,
    OP_AND, OP_OR, OP_XOR, OP_IMP, OP_EQV, OP_STRCAT,
} binop_t;

/* Unary operators */
typedef enum {
    OP_NEG, OP_NOT,
} unaryop_t;

/* Comparison operators */
typedef enum {
    CMP_EQ, CMP_NE, CMP_LT, CMP_GT, CMP_LE, CMP_GE,
} cmpop_t;

/* Expression node */
typedef struct expr {
    expr_type_t type;
    int line;
    union {
        /* EXPR_LITERAL */
        value_t literal;

        /* EXPR_VARIABLE */
        struct {
            char name[64];
            val_type_t var_type;
        } var;

        /* EXPR_UNARY */
        struct {
            unaryop_t op;
            struct expr *operand;
        } unary;

        /* EXPR_BINARY */
        struct {
            binop_t op;
            struct expr *left;
            struct expr *right;
        } binary;

        /* EXPR_COMPARE */
        struct {
            cmpop_t op;
            struct expr *left;
            struct expr *right;
        } compare;

        /* EXPR_CALL */
        struct {
            char name[64];
            struct expr *args[8];
            int nargs;
        } call;

        /* EXPR_FIELD_ACCESS */
        struct {
            char var_name[64];
            char field_name[64];
        } field;
    };
} expr_t;

/* Statement node types */
typedef enum {
    STMT_PRINT,
    STMT_INPUT,
    STMT_CLS,
    STMT_LOCATE,
    STMT_COLOR,
    STMT_ASSIGN,
    STMT_IF,
    STMT_FOR,
    STMT_WHILE,
    STMT_END,
    STMT_REM,
    /* Stage 2 */
    STMT_DO_LOOP,
    STMT_SELECT,
    STMT_GOTO,
    STMT_GOSUB,
    STMT_RETURN,
    STMT_LABEL,
    STMT_SUB_DEF,
    STMT_FUNC_DEF,
    STMT_CALL,
    STMT_EXIT,
    STMT_CONST,
    STMT_SHARED,
    STMT_STATIC,
    STMT_DECLARE,
    /* Stage 3 */
    STMT_DIM,
    STMT_ARRAY_ASSIGN,
    STMT_OPTION_BASE,
    STMT_ERASE,
    STMT_DATA,
    STMT_READ,
    STMT_RESTORE,
    /* Stage 4 */
    STMT_SWAP,
    STMT_RANDOMIZE,
    STMT_SLEEP,
    /* Stage 5 */
    STMT_OPEN,
    STMT_CLOSE,
    STMT_PRINT_FILE,
    STMT_INPUT_FILE,
    STMT_LINE_INPUT,
    STMT_WRITE_FILE,
    STMT_KILL,
    STMT_NAME,
    STMT_GET,
    STMT_PUT,
    STMT_SEEK,
    /* Stage 6 */
    STMT_TYPE_DEF,
    STMT_DIM_AS_TYPE,
    STMT_FIELD_ASSIGN,
    STMT_MID_ASSIGN,
    STMT_ON_ERROR,
    STMT_RESUME,
    STMT_ERROR_RAISE,
    STMT_DEFTYPE,
    STMT_ON_GOTO,
    STMT_ON_GOSUB,
    /* Extra statements */
    STMT_BEEP,
    STMT_TRACE,
    STMT_SPLIT,
    STMT_OPTION_EXPLICIT,
    STMT_DIM_SCALAR,
    STMT_LINE_INPUT_CONSOLE,
    STMT_WRITE_CONSOLE,
    STMT_DEF_FN,
    STMT_SHELL,
    STMT_CHDIR,
    STMT_MKDIR,
    STMT_RMDIR,
    STMT_LPRINT,
    STMT_POKE,
    STMT_SCREEN,
    STMT_DEF_SEG,
    STMT_LSET,
    STMT_RSET,
    STMT_WIDTH,
    STMT_NOOP,  /* parsed-and-discarded: FIELD, CHAIN, PCOPY, KEY, SOUND, PLAY, VIEW PRINT, ENVIRON, COMMON */
    STMT_CLEAR,
} stmt_type_t;

/* Print item: expression + separator */
typedef struct {
    expr_t *expr;       /* NULL for trailing separator */
    char sep;           /* ';' ',' or '\0' (newline) */
} print_item_t;

/* Exit target */
typedef enum {
    EXIT_FOR, EXIT_WHILE, EXIT_DO, EXIT_SUB, EXIT_FUNCTION,
} exit_type_t;

/* Case match within SELECT CASE */
typedef struct case_match {
    int match_type;     /* 0=value, 1=range, 2=IS comparison */
    expr_t *expr1;      /* value, or range low, or IS rhs */
    expr_t *expr2;      /* range high (match_type==1 only) */
    cmpop_t is_op;      /* comparison op (match_type==2 only) */
} case_match_t;

/* Case clause in SELECT CASE */
typedef struct case_clause {
    case_match_t *matches;
    int nmatches;
    int is_else;
    struct stmt *body;
    struct case_clause *next;
} case_clause_t;

/* Statement node */
typedef struct stmt {
    stmt_type_t type;
    int line;
    struct stmt *next;  /* linked list of statements */
    union {
        /* STMT_PRINT */
        struct {
            print_item_t *items;
            int nitems;
            char *using_fmt;    /* PRINT USING format string literal (NULL = normal) */
            struct expr *using_expr; /* PRINT USING <expr>; ... (NULL = literal or normal) */
        } print;

        /* STMT_INPUT */
        struct {
            char *prompt;
            char (*varnames)[64];
            val_type_t *vartypes;
            int nvars;
            int var_cap;
        } input;

        /* STMT_LOCATE */
        struct {
            expr_t *row;
            expr_t *col;
        } locate_stmt;

        /* STMT_COLOR */
        struct {
            expr_t *fg;
            expr_t *bg;
            int has_fg;
            int has_bg;
        } color_stmt;

        /* STMT_ASSIGN */
        struct {
            char name[64];
            val_type_t var_type;
            expr_t *value;
        } assign;

        /* STMT_IF */
        struct {
            expr_t *condition;
            struct stmt *then_body;
            struct stmt *else_body;
        } if_stmt;

        /* STMT_FOR */
        struct {
            char var_name[64];
            val_type_t var_type;
            expr_t *start;
            expr_t *end;
            expr_t *step;
            struct stmt *body;
        } for_stmt;

        /* STMT_WHILE */
        struct {
            expr_t *condition;
            struct stmt *body;
        } while_stmt;

        /* STMT_DO_LOOP */
        struct {
            expr_t *pre_cond;    /* condition after DO (may be NULL) */
            expr_t *post_cond;   /* condition after LOOP (may be NULL) */
            int pre_until;       /* 1=UNTIL (inverted), 0=WHILE */
            int post_until;
            struct stmt *body;
        } do_loop;

        /* STMT_SELECT */
        struct {
            expr_t *test_expr;
            case_clause_t *clauses;
        } select_stmt;

        /* STMT_GOTO / STMT_GOSUB */
        struct {
            char label[64];
        } goto_stmt;

        /* STMT_LABEL */
        struct {
            char name[64];
        } label;

        /* STMT_SUB_DEF / STMT_FUNC_DEF */
        struct {
            char name[64];
            char params[16][64];
            val_type_t param_types[16];
            int nparams;
            val_type_t return_type; /* FUNC_DEF only */
            struct stmt *body;
        } proc_def;

        /* STMT_CALL */
        struct {
            char name[64];
            expr_t *args[16];
            int nargs;
        } call_stmt;

        /* STMT_EXIT */
        struct {
            exit_type_t what;
        } exit_stmt;

        /* STMT_CONST */
        struct {
            char name[64];
            val_type_t var_type;
            expr_t *value;
        } const_stmt;

        /* STMT_SHARED */
        struct {
            char varnames[16][64];
            int nvars;
        } shared;

        /* STMT_DIM / STMT_REDIM */
        struct {
            char name[64];
            val_type_t elem_type;
            int ndims;
            struct expr *dims[8];   /* upper bound per dimension */
            int is_redim;
            int preserve;
        } dim_stmt;

        /* STMT_ARRAY_ASSIGN */
        struct {
            char name[64];
            val_type_t var_type;
            int nindices;
            struct expr *indices[8];
            struct expr *value;
        } array_assign;

        /* STMT_OPTION_BASE */
        struct {
            int base;
        } option_base;

        /* STMT_ERASE */
        struct {
            char names[8][64];
            int nnames;
        } erase_stmt;

        /* STMT_DATA */
        struct {
            value_t *values;
            int nvalues;
        } data_stmt;

        /* STMT_READ */
        struct {
            char (*varnames)[64];
            val_type_t *vartypes;
            int nvars;
            int var_cap;
        } read_stmt;

        /* STMT_RESTORE */
        struct {
            char label[64];
        } restore_stmt;

        /* STMT_SWAP */
        struct {
            expr_t *lhs;
            expr_t *rhs;
        } swap_stmt;

        /* STMT_RANDOMIZE */
        struct {
            struct expr *seed;   /* NULL = use default */
        } randomize;

        /* STMT_SLEEP */
        struct {
            struct expr *duration;  /* NULL = SLEEP 0 */
        } sleep_stmt;

        /* STMT_OPEN */
        struct {
            struct expr *filename;
            int mode;            /* FMODE_INPUT, FMODE_OUTPUT, FMODE_APPEND, FMODE_BINARY, FMODE_RANDOM */
            struct expr *handle_num;
            struct expr *reclen; /* record length for RANDOM (NULL for others) */
        } open_stmt;

        /* STMT_CLOSE */
        struct {
            struct expr *handle_num; /* NULL = close all */
        } close_stmt;

        /* STMT_PRINT_FILE / STMT_WRITE_FILE */
        struct {
            struct expr *handle_num;
            print_item_t *items;
            int nitems;
            char *using_fmt;         /* PRINT #n, USING "fmt"; ... */
            struct expr *using_expr; /* PRINT #n, USING expr; ... */
        } print_file;

        /* STMT_INPUT_FILE / STMT_LINE_INPUT */
        struct {
            struct expr *handle_num;
            char (*varnames)[64];
            val_type_t *vartypes;
            int nvars;
            int var_cap;
        } input_file;

        /* STMT_KILL */
        struct {
            struct expr *filename;
        } kill_stmt;

        /* STMT_NAME */
        struct {
            struct expr *oldname;
            struct expr *newname;
        } name_stmt;

        /* STMT_GET / STMT_PUT */
        struct {
            struct expr *handle_num;
            struct expr *position;     /* NULL = current pos */
            char varname[64];
            val_type_t vartype;
        } get_put_stmt;

        /* STMT_SEEK */
        struct {
            struct expr *seek_handle;
            struct expr *seek_position;
        } seek_stmt;

        /* STMT_TYPE_DEF */
        struct {
            char name[64];
            char field_names[32][64];
            val_type_t field_types[32];
            int nfields;
        } type_def;

        /* STMT_DIM_AS_TYPE */
        struct {
            char name[64];
            char type_name[64];
        } dim_as_type;

        /* STMT_FIELD_ASSIGN: var.field = expr */
        struct {
            char var_name[64];
            char field_name[64];
            struct expr *value;
        } field_assign;

        /* STMT_MID_ASSIGN: MID$(var$, start [, length]) = expr */
        struct {
            char var_name[64];
            struct expr *start;
            struct expr *length;  /* NULL if omitted */
            struct expr *value;
        } mid_assign;

        /* STMT_ON_ERROR */
        struct {
            char label[64];     /* "" = ON ERROR GOTO 0 (disable) */
        } on_error;

        /* STMT_RESUME */
        struct {
            int resume_next;    /* 0 = RESUME, 1 = RESUME NEXT, 2 = RESUME label */
            char label[64];     /* target label (when resume_next == 2) */
        } resume_stmt;

        /* STMT_ERROR_RAISE */
        struct {
            struct expr *errnum;
        } error_raise;

        /* STMT_DEFTYPE */
        struct {
            val_type_t def_type;  /* VAL_INTEGER, VAL_DOUBLE, VAL_STRING */
            char from;            /* first letter */
            char to;              /* last letter */
        } deftype;

        /* STMT_ON_GOTO / STMT_ON_GOSUB */
        struct {
            struct expr *index;
            char labels[16][64];
            int nlabels;
        } on_branch;

        /* STMT_DIM_SCALAR: DIM x AS INTEGER/DOUBLE/STRING */
        struct {
            char name[64];
            val_type_t scalar_type;
        } dim_scalar;

        /* STMT_TRACE */
        struct {
            int enabled;        /* 1 = ON, 0 = OFF */
        } trace;

        /* STMT_SPLIT: SPLIT str_expr, delim_expr, array_name$() */
        struct {
            struct expr *str_expr;
            struct expr *delim_expr;
            char array_name[64];
        } split;

        /* STMT_DEF_FN: DEF FNname(params) = expr */
        struct {
            char name[64];
            char params[8][64];
            val_type_t param_types[8];
            int nparams;
            struct expr *body;
        } def_fn;

        /* STMT_SHELL / STMT_CHDIR / STMT_MKDIR / STMT_RMDIR / STMT_SCREEN / STMT_DEF_SEG */
        struct {
            struct expr *command;
        } shell_stmt;

        /* STMT_POKE */
        struct {
            struct expr *addr;
            struct expr *value;
        } poke_stmt;

        /* STMT_LSET / STMT_RSET */
        struct {
            char varname[64];
            struct expr *value;
        } lrset;

        /* STMT_WIDTH */
        struct {
            struct expr *columns;
        } width_stmt;
    };
} stmt_t;

/* Expression constructors */
expr_t *expr_literal(value_t val, int line);
expr_t *expr_variable(const char *name, val_type_t type, int line);
expr_t *expr_unary(unaryop_t op, expr_t *operand, int line);
expr_t *expr_binary(binop_t op, expr_t *left, expr_t *right, int line);
expr_t *expr_compare(cmpop_t op, expr_t *left, expr_t *right, int line);
expr_t *expr_call(const char *name, int line);
void expr_call_add_arg(expr_t *call, expr_t *arg);
void expr_free(expr_t *e);

/* Statement constructors */
stmt_t *stmt_alloc(stmt_type_t type, int line);
stmt_t *stmt_print(int line);
int stmt_print_add(stmt_t *s, expr_t *expr, char sep);
stmt_t *stmt_input(const char *prompt, int line);
int stmt_input_add_var(stmt_t *s, const char *name, val_type_t type);
stmt_t *stmt_cls(int line);
stmt_t *stmt_locate(expr_t *row, expr_t *col, int line);
stmt_t *stmt_color(expr_t *fg, expr_t *bg, int has_fg, int has_bg, int line);
stmt_t *stmt_assign(const char *name, val_type_t type, expr_t *value, int line);
stmt_t *stmt_if(expr_t *cond, stmt_t *then_body, stmt_t *else_body, int line);
stmt_t *stmt_for(const char *var, val_type_t type,
                 expr_t *start, expr_t *end, expr_t *step,
                 stmt_t *body, int line);
stmt_t *stmt_while(expr_t *cond, stmt_t *body, int line);
stmt_t *stmt_end(int line);

/* Stage 2 constructors */
stmt_t *stmt_do_loop(expr_t *pre_cond, int pre_until,
                     expr_t *post_cond, int post_until,
                     stmt_t *body, int line);
stmt_t *stmt_select(expr_t *test, case_clause_t *clauses, int line);
stmt_t *stmt_goto(const char *label, int line);
stmt_t *stmt_gosub(const char *label, int line);
stmt_t *stmt_return(const char *label, int line);
stmt_t *stmt_label(const char *name, int line);
stmt_t *stmt_proc_def(stmt_type_t type, const char *name, int line);
void stmt_proc_add_param(stmt_t *s, const char *name, val_type_t type);
void stmt_proc_set_body(stmt_t *s, stmt_t *body);
stmt_t *stmt_call(const char *name, int line);
void stmt_call_add_arg(stmt_t *s, expr_t *arg);
stmt_t *stmt_exit(exit_type_t what, int line);
stmt_t *stmt_const(const char *name, val_type_t type, expr_t *value, int line);
stmt_t *stmt_shared(int line);
void stmt_shared_add(stmt_t *s, const char *name);
stmt_t *stmt_static_decl(int line);
void stmt_static_add(stmt_t *s, const char *name);

/* Case clause constructors */
case_clause_t *case_clause_alloc(void);
int case_clause_add_value(case_clause_t *c, expr_t *val);
int case_clause_add_range(case_clause_t *c, expr_t *lo, expr_t *hi);
int case_clause_add_is(case_clause_t *c, cmpop_t op, expr_t *val);
void case_clause_free(case_clause_t *c);

/* Statement destructor (frees entire chain) */
void stmt_free(stmt_t *s);

/* Stage 3 constructors */
stmt_t *stmt_dim(const char *name, val_type_t type,
                 expr_t **dims, int ndims,
                 int is_redim, int preserve, int line);
stmt_t *stmt_array_assign(const char *name, val_type_t type,
                          expr_t **indices, int nindices,
                          expr_t *value, int line);
stmt_t *stmt_option_base(int base, int line);
stmt_t *stmt_erase(int line);
void stmt_erase_add(stmt_t *s, const char *name);
stmt_t *stmt_data(int line);
int stmt_data_add(stmt_t *s, value_t val);
stmt_t *stmt_read(int line);
int stmt_read_add_var(stmt_t *s, const char *name, val_type_t type);
stmt_t *stmt_restore(const char *label, int line);

/* Stage 4 constructors */
stmt_t *stmt_swap(expr_t *lhs, expr_t *rhs, int line);
stmt_t *stmt_randomize(expr_t *seed, int line);
stmt_t *stmt_sleep(expr_t *duration, int line);

/* Stage 5 constructors */
stmt_t *stmt_open(expr_t *filename, int mode, expr_t *handle,
                   expr_t *reclen, int line);
stmt_t *stmt_close(expr_t *handle, int line);
stmt_t *stmt_print_file(expr_t *handle, int line);
int stmt_print_file_add(stmt_t *s, expr_t *expr, char sep);
stmt_t *stmt_write_file(expr_t *handle, int line);
int stmt_write_file_add(stmt_t *s, expr_t *expr, char sep);
stmt_t *stmt_input_file(expr_t *handle, int line);
int stmt_input_file_add_var(stmt_t *s, const char *name, val_type_t type);
stmt_t *stmt_line_input(expr_t *handle, int line);
stmt_t *stmt_kill(expr_t *filename, int line);
stmt_t *stmt_name(expr_t *oldname, expr_t *newname, int line);
stmt_t *stmt_get(expr_t *handle, expr_t *position,
                 const char *varname, val_type_t vartype, int line);
stmt_t *stmt_put(expr_t *handle, expr_t *position,
                 const char *varname, val_type_t vartype, int line);
stmt_t *stmt_seek(expr_t *handle, expr_t *position, int line);

/* Stage 6 constructors */
expr_t *expr_field_access(const char *var, const char *field, int line);
stmt_t *stmt_type_def(const char *name, int line);
void stmt_type_def_add_field(stmt_t *s, const char *name, val_type_t type);
stmt_t *stmt_dim_as_type(const char *name, const char *type_name, int line);
stmt_t *stmt_field_assign(const char *var, const char *field,
                          expr_t *value, int line);
stmt_t *stmt_mid_assign(const char *var, expr_t *start, expr_t *length,
                         expr_t *value, int line);
stmt_t *stmt_on_error(const char *label, int line);
stmt_t *stmt_resume(int resume_next, const char *label, int line);
stmt_t *stmt_error_raise(expr_t *errnum, int line);
stmt_t *stmt_deftype(val_type_t type, char from, char to, int line);
stmt_t *stmt_on_goto(expr_t *index, int line);
stmt_t *stmt_on_gosub(expr_t *index, int line);
void stmt_on_branch_add_label(stmt_t *s, const char *label);

/* Append statement to end of chain */
void stmt_append(stmt_t **head, stmt_t **tail, stmt_t *s);

#endif
