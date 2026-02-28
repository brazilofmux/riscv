#include "eval.h"
#include "builtin.h"
#include "array.h"
#include "fileio.h"
#include "terminal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

/* --- Procedure and label tables --- */

#define MAX_PROCS 128
#define MAX_LABELS 256
#define MAX_GOSUB_DEPTH 256
#define MAX_DATA_VALUES 1024

typedef struct {
    char name[64];
    stmt_t *def;
    env_t *static_env;  /* persistent env for STATIC variables */
} proc_entry_t;

typedef struct {
    char name[64];
    int index;          /* statement index for GOTO */
    int data_offset;    /* data pool offset for RESTORE */
} label_entry_t;

static proc_entry_t proc_table[MAX_PROCS];
static int proc_count = 0;

static label_entry_t label_table[MAX_LABELS];
static int label_count = 0;

static int gosub_stack[MAX_GOSUB_DEPTH];
static int gosub_top = 0;

static int goto_target = -1;

/* Pointer to global env for SHARED variable access */
static env_t *program_global_env = NULL;

/* OPTION BASE setting */
static int option_base = 0;

/* DATA pool */
static value_t data_pool[MAX_DATA_VALUES];
static int data_pool_count = 0;
static int data_read_ptr = 0;

/* TYPE record definitions */
#define MAX_TYPE_DEFS 32

typedef struct {
    char name[64];
    char field_names[32][64];
    val_type_t field_types[32];
    int nfields;
} type_def_t;

static type_def_t type_defs[MAX_TYPE_DEFS];
static int type_def_count = 0;

/* ON ERROR state */
static int on_error_label_idx = -1;  /* -1 = no handler */
static int on_error_active = 0;      /* currently handling an error? */
static int on_error_err_code = 0;    /* ERR value */
static int on_error_err_line = 0;    /* ERL value */
static int on_error_resume_pc = 0;   /* PC to RESUME to */
static int on_error_resume_next_pc = 0; /* PC for RESUME NEXT */
static int resume_is_next = 0;       /* set by RESUME stmt for eval_program: 0=retry, 1=next, 2=label */
static char resume_label[64] = "";   /* target label for RESUME label */
static char return_label[64] = "";   /* target label for RETURN label */

/* Recursion depth limit (guards against C stack overflow) */
#define MAX_EVAL_DEPTH 48
static int eval_depth = 0;

/* TRACE mode: print line numbers as they execute */
static int trace_enabled = 0;

/* PEEK/POKE simulated memory (64KB) */
#define PEEK_POKE_SIZE 65536
unsigned char peek_poke_mem[PEEK_POKE_SIZE];

/* OPTION EXPLICIT: require variables to be declared before use */
static int option_explicit = 0;

/* Is this a real error (trappable by ON ERROR) or a flow-control signal? */
static int is_trappable_error(error_t err) {
    switch (err) {
    case ERR_NONE:
    case ERR_EXIT:
    case ERR_BREAK:
    case ERR_EXIT_FOR:
    case ERR_EXIT_WHILE:
    case ERR_EXIT_DO:
    case ERR_EXIT_SUB:
    case ERR_EXIT_FUNCTION:
    case ERR_GOTO:
    case ERR_GOSUB:
    case ERR_RETURN:
    case ERR_RESUME_WITHOUT_ERROR:
    case ERR_ON_ERROR_GOTO:
        return 0;
    default:
        return 1;
    }
}

/* DEFTYPE letter-to-type map: 26 letters A-Z, default VAL_DOUBLE */
static val_type_t deftype_map[26];

static val_type_t deftype_lookup(char first_letter) {
    if (first_letter >= 'A' && first_letter <= 'Z')
        return deftype_map[first_letter - 'A'];
    if (first_letter >= 'a' && first_letter <= 'z')
        return deftype_map[first_letter - 'a'];
    return VAL_DOUBLE;
}

static void deftype_init(void) {
    for (int i = 0; i < 26; i++)
        deftype_map[i] = VAL_DOUBLE;
    env_deftype_hook = deftype_lookup;
}

/* Resolve the effective type for a variable name.
   Explicit suffixes ($, %, #) take priority; bare names use deftype_map. */
static val_type_t resolve_var_type(const char *name) {
    int len = (int)strlen(name);
    if (len > 0) {
        char last = name[len - 1];
        if (last == '$') return VAL_STRING;
        if (last == '%') return VAL_INTEGER;
        if (last == '#') return VAL_DOUBLE;
    }
    return deftype_lookup(name[0]);
}

/* Find a TYPE definition by name */
static int find_type_def(const char *name) {
    for (int i = 0; i < type_def_count; i++) {
        if (strcmp(type_defs[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* Find field index in a type def */
static int find_field_index(type_def_t *td, const char *field) {
    for (int i = 0; i < td->nfields; i++) {
        if (strcmp(td->field_names[i], field) == 0)
            return i;
    }
    return -1;
}

/* --- Lookup helpers --- */

static proc_entry_t *find_proc(const char *name) {
    for (int i = 0; i < proc_count; i++) {
        if (strcmp(proc_table[i].name, name) == 0)
            return &proc_table[i];
    }
    return NULL;
}

static int find_label(const char *name) {
    for (int i = 0; i < label_count; i++) {
        if (strcmp(label_table[i].name, name) == 0)
            return label_table[i].index;
    }
    return -1;
}

static int find_label_data_offset(const char *name) {
    for (int i = 0; i < label_count; i++) {
        if (strcmp(label_table[i].name, name) == 0)
            return label_table[i].data_offset;
    }
    return -1;
}

/* --- User-defined procedure call --- */

static error_t call_proc(env_t *caller_env, proc_entry_t *proc,
                          expr_t **arg_exprs, int nargs, value_t *out) {
    stmt_t *def = proc->def;
    int is_function = (def->type == STMT_FUNC_DEF);

    if (nargs != def->proc_def.nparams)
        return ERR_ILLEGAL_FUNCTION_CALL;

    value_t arg_vals[16];
    for (int i = 0; i < nargs; i++) {
        error_t err = eval_expr(caller_env, arg_exprs[i], &arg_vals[i]);
        if (err != ERR_NONE) {
            for (int j = 0; j < i; j++) val_clear(&arg_vals[j]);
            return err;
        }
    }

    env_t *local = env_create(NULL);

    for (int i = 0; i < nargs; i++) {
        env_set(local, def->proc_def.params[i], &arg_vals[i]);
        val_clear(&arg_vals[i]);
    }

    /* Collect and copy-in SHARED variables (pointers into AST, no copy) */
    const char *shared_names[64];
    int nshared = 0;
    for (stmt_t *s = def->proc_def.body; s && nshared < 64; s = s->next) {
        if (s->type == STMT_SHARED) {
            for (int i = 0; i < s->shared.nvars && nshared < 64; i++) {
                shared_names[nshared] = s->shared.varnames[i];
                nshared++;
            }
        }
    }
    for (int i = 0; i < nshared; i++) {
        value_t *gv = env_get(program_global_env, shared_names[i]);
        if (gv) env_link(local, shared_names[i], gv);
    }

    /* Collect STATIC variables and link to persistent env */
    const char *static_names[64];
    int nstatic = 0;
    for (stmt_t *s = def->proc_def.body; s && nstatic < 64; s = s->next) {
        if (s->type == STMT_STATIC) {
            for (int i = 0; i < s->shared.nvars && nstatic < 64; i++) {
                static_names[nstatic] = s->shared.varnames[i];
                nstatic++;
            }
        }
    }
    if (nstatic > 0) {
        if (!proc->static_env)
            proc->static_env = env_create(NULL);
        for (int i = 0; i < nstatic; i++) {
            /* env_get auto-creates with default on first access */
            value_t *sv = env_get(proc->static_env, static_names[i]);
            if (sv) env_link(local, static_names[i], sv);
        }
    }

    error_t err = eval_stmts(local, def->proc_def.body);

    if (err == ERR_EXIT_SUB && !is_function) err = ERR_NONE;
    if (err == ERR_EXIT_FUNCTION && is_function) err = ERR_NONE;

    if (is_function && out) {
        if (err == ERR_NONE) {
            value_t *retval = env_get(local, def->proc_def.name);
            if (retval)
                *out = val_copy(retval);
            else
                *out = val_double(0.0);
        }
    }

    env_destroy(local);
    return err;
}

/* --- Expression evaluation --- */

static error_t eval_expr_impl(env_t *env, expr_t *e, value_t *out);

error_t eval_expr(env_t *env, expr_t *e, value_t *out) {
    if (++eval_depth > MAX_EVAL_DEPTH) {
        eval_depth--;
        return ERR_STACK_OVERFLOW;
    }
    error_t err = eval_expr_impl(env, e, out);
    eval_depth--;
    return err;
}

static error_t eval_expr_impl(env_t *env, expr_t *e, value_t *out) {
    if (!e) return ERR_INTERNAL;

    switch (e->type) {
        case EXPR_LITERAL:
            *out = val_copy(&e->literal);
            return ERR_NONE;

        case EXPR_VARIABLE: {
            /* ERR and ERL pseudo-variables */
            if (strcmp(e->var.name, "ERR") == 0) {
                *out = val_integer(on_error_err_code);
                return ERR_NONE;
            }
            if (strcmp(e->var.name, "ERL") == 0) {
                *out = val_integer(on_error_err_line);
                return ERR_NONE;
            }
            value_t *v = env_get(env, e->var.name);
            if (!v) return ERR_OUT_OF_MEMORY;
            *out = val_copy(v);
            return ERR_NONE;
        }

        case EXPR_UNARY: {
            value_t operand;
            EVAL_CHECK(eval_expr(env, e->unary.operand, &operand));
            error_t err;
            if (e->unary.op == OP_NEG) {
                err = val_neg(&operand, out);
            } else {
                double dv;
                err = val_to_double(&operand, &dv);
                if (err == ERR_NONE)
                    *out = val_integer(~(int)dv);
            }
            val_clear(&operand);
            return err;
        }

        case EXPR_BINARY: {
            value_t left, right;
            EVAL_CHECK(eval_expr(env, e->binary.left, &left));
            error_t err = eval_expr(env, e->binary.right, &right);
            if (err != ERR_NONE) {
                val_clear(&left);
                return err;
            }

            switch (e->binary.op) {
                case OP_ADD:  err = val_add(&left, &right, out); break;
                case OP_STRCAT: {
                    /* & operator: coerce both to strings and concatenate */
                    char lbuf[64], rbuf[64];
                    const char *ls, *rs;
                    int llen, rlen;
                    if (left.type == VAL_STRING) {
                        ls = left.sval ? left.sval->data : "";
                        llen = left.sval ? left.sval->len : 0;
                    } else if (left.type == VAL_INTEGER) {
                        snprintf(lbuf, sizeof(lbuf), "%d", left.ival);
                        ls = lbuf; llen = (int)strlen(lbuf);
                    } else {
                        snprintf(lbuf, sizeof(lbuf), "%g", left.dval);
                        ls = lbuf; llen = (int)strlen(lbuf);
                    }
                    if (right.type == VAL_STRING) {
                        rs = right.sval ? right.sval->data : "";
                        rlen = right.sval ? right.sval->len : 0;
                    } else if (right.type == VAL_INTEGER) {
                        snprintf(rbuf, sizeof(rbuf), "%d", right.ival);
                        rs = rbuf; rlen = (int)strlen(rbuf);
                    } else {
                        snprintf(rbuf, sizeof(rbuf), "%g", right.dval);
                        rs = rbuf; rlen = (int)strlen(rbuf);
                    }
                    int total = llen + rlen;
                    char *buf = malloc(total + 1);
                    if (!buf) { err = ERR_OUT_OF_MEMORY; break; }
                    memcpy(buf, ls, llen);
                    memcpy(buf + llen, rs, rlen);
                    buf[total] = '\0';
                    *out = val_string(buf, total);
                    free(buf);
                    break;
                }
                case OP_SUB:  err = val_sub(&left, &right, out); break;
                case OP_MUL:  err = val_mul(&left, &right, out); break;
                case OP_DIV:  err = val_div(&left, &right, out); break;
                case OP_IDIV: err = val_idiv(&left, &right, out); break;
                case OP_MOD:  err = val_mod(&left, &right, out); break;
                case OP_POW:  err = val_pow(&left, &right, out); break;
                case OP_AND: case OP_OR: case OP_XOR: case OP_IMP: case OP_EQV: {
                    double dl, dr;
                    err = val_to_double(&left, &dl);
                    if (err == ERR_NONE) err = val_to_double(&right, &dr);
                    if (err == ERR_NONE) {
                        int il = (int)dl, ir = (int)dr;
                        if (e->binary.op == OP_AND) *out = val_integer(il & ir);
                        else if (e->binary.op == OP_OR) *out = val_integer(il | ir);
                        else if (e->binary.op == OP_XOR) *out = val_integer(il ^ ir);
                        else if (e->binary.op == OP_IMP) *out = val_integer(~il | ir);
                        else *out = val_integer(~(il ^ ir));
                    }
                    break;
                }
                default:      err = ERR_INTERNAL; break;
            }
            val_clear(&left);
            val_clear(&right);
            return err;
        }

        case EXPR_COMPARE: {
            value_t left, right;
            EVAL_CHECK(eval_expr(env, e->compare.left, &left));
            error_t err = eval_expr(env, e->compare.right, &right);
            if (err != ERR_NONE) {
                val_clear(&left);
                return err;
            }
            int cmp;
            err = val_compare(&left, &right, &cmp);
            val_clear(&left);
            val_clear(&right);
            if (err != ERR_NONE) return err;

            int result;
            switch (e->compare.op) {
                case CMP_EQ: result = (cmp == 0); break;
                case CMP_NE: result = (cmp != 0); break;
                case CMP_LT: result = (cmp < 0); break;
                case CMP_GT: result = (cmp > 0); break;
                case CMP_LE: result = (cmp <= 0); break;
                case CMP_GE: result = (cmp >= 0); break;
                default:     result = 0; break;
            }
            *out = val_integer(result ? -1 : 0);
            return ERR_NONE;
        }

        case EXPR_CALL: {
            /* 1. Check for user-defined function */
            proc_entry_t *proc = find_proc(e->call.name);
            if (proc && proc->def->type == STMT_FUNC_DEF)
                return call_proc(env, proc, e->call.args, e->call.nargs, out);
            if (proc && proc->def->type == STMT_DEF_FN) {
                /* DEF FN: evaluate body expression with params bound */
                stmt_t *def = proc->def;
                if (e->call.nargs != def->def_fn.nparams)
                    return ERR_ILLEGAL_FUNCTION_CALL;
                env_t *fn_env = env_create(env);
                for (int i = 0; i < def->def_fn.nparams; i++) {
                    value_t av;
                    error_t err = eval_expr(env, e->call.args[i], &av);
                    if (err != ERR_NONE) { env_destroy(fn_env); return err; }
                    env_set(fn_env, def->def_fn.params[i], &av);
                    val_clear(&av);
                }
                error_t err = eval_expr(fn_env, def->def_fn.body, out);
                env_destroy(fn_env);
                return err;
            }

            /* 2. Check for array element access */
            sb_array_t *arr = array_find(e->call.name);
            if (arr) {
                int indices[MAX_ARRAY_DIMS];
                if (e->call.nargs != arr->ndims)
                    return ERR_SUBSCRIPT_OUT_OF_RANGE;
                for (int i = 0; i < e->call.nargs; i++) {
                    value_t iv;
                    error_t err = eval_expr(env, e->call.args[i], &iv);
                    if (err != ERR_NONE) return err;
                    err = val_to_integer(&iv, &indices[i]);
                    val_clear(&iv);
                    if (err != ERR_NONE) return err;
                }
                value_t *elem;
                error_t err = array_get(e->call.name, indices,
                                        e->call.nargs, &elem);
                if (err != ERR_NONE) return err;
                *out = val_copy(elem);
                return ERR_NONE;
            }

            /* 3. LBOUND / UBOUND special handling */
            if (strcmp(e->call.name, "LBOUND") == 0 ||
                strcmp(e->call.name, "UBOUND") == 0) {
                if (e->call.nargs < 1) return ERR_ILLEGAL_FUNCTION_CALL;
                const char *arrname;
                if (e->call.args[0]->type == EXPR_VARIABLE)
                    arrname = e->call.args[0]->var.name;
                else if (e->call.args[0]->type == EXPR_CALL && e->call.args[0]->call.nargs == 0)
                    arrname = e->call.args[0]->call.name;  /* arr() syntax */
                else
                    return ERR_ILLEGAL_FUNCTION_CALL;
                int dim = 1;
                if (e->call.nargs >= 2) {
                    value_t dv;
                    EVAL_CHECK(eval_expr(env, e->call.args[1], &dv));
                    error_t err = val_to_integer(&dv, &dim);
                    val_clear(&dv);
                    if (err != ERR_NONE) return err;
                }
                sb_array_t *a = array_find(arrname);
                if (!a) return ERR_UNDEFINED_VAR;
                if (dim < 1 || dim > a->ndims)
                    return ERR_ILLEGAL_FUNCTION_CALL;
                if (strcmp(e->call.name, "LBOUND") == 0)
                    *out = val_integer(a->base);
                else
                    *out = val_integer(a->base + a->dims[dim - 1] - 1);
                return ERR_NONE;
            }

            /* 3b. JOIN$ special handling: JOIN$(array$(), delim$) */
            if (strcmp(e->call.name, "JOIN$") == 0) {
                if (e->call.nargs < 1 || e->call.nargs > 2)
                    return ERR_ILLEGAL_FUNCTION_CALL;
                const char *arrname;
                if (e->call.args[0]->type == EXPR_VARIABLE)
                    arrname = e->call.args[0]->var.name;
                else if (e->call.args[0]->type == EXPR_CALL && e->call.args[0]->call.nargs == 0)
                    arrname = e->call.args[0]->call.name;  /* arr$() syntax */
                else
                    return ERR_ILLEGAL_FUNCTION_CALL;
                /* Default delimiter is empty string */
                char delim[256] = "";
                if (e->call.nargs == 2) {
                    value_t dv;
                    EVAL_CHECK(eval_expr(env, e->call.args[1], &dv));
                    if (dv.type != VAL_STRING) { val_clear(&dv); return ERR_TYPE_MISMATCH; }
                    strncpy(delim, dv.sval->data, 255);
                    delim[255] = '\0';
                    val_clear(&dv);
                }
                sb_array_t *a = array_find(arrname);
                if (!a) return ERR_UNDEFINED_VAR;
                int dlen = (int)strlen(delim);
                /* Calculate total length */
                int total_len = 0;
                for (int i = 0; i < a->total; i++) {
                    if (i > 0) total_len += dlen;
                    if (a->data[i].type == VAL_STRING && a->data[i].sval)
                        total_len += a->data[i].sval->len;
                }
                if (total_len > MAX_STRING_LEN) total_len = MAX_STRING_LEN;
                char *buf = malloc(total_len + 1);
                if (!buf) return ERR_OUT_OF_MEMORY;
                int pos = 0;
                for (int i = 0; i < a->total; i++) {
                    if (i > 0 && dlen > 0 && pos + dlen <= total_len) {
                        memcpy(buf + pos, delim, dlen);
                        pos += dlen;
                    }
                    if (a->data[i].type == VAL_STRING && a->data[i].sval) {
                        int slen = a->data[i].sval->len;
                        if (pos + slen > total_len) slen = total_len - pos;
                        memcpy(buf + pos, a->data[i].sval->data, slen);
                        pos += slen;
                    }
                }
                buf[pos] = '\0';
                *out = val_string(buf, pos);
                free(buf);
                return ERR_NONE;
            }

            /* 4. Built-in functions */
            value_t args[8];
            int nargs = e->call.nargs;
            for (int i = 0; i < nargs; i++) {
                error_t err = eval_expr(env, e->call.args[i], &args[i]);
                if (err != ERR_NONE) {
                    for (int j = 0; j < i; j++) val_clear(&args[j]);
                    return err;
                }
            }
            /* Check for ERR and ERL pseudo-functions */
            if (strcmp(e->call.name, "ERR") == 0 && nargs == 0) {
                *out = val_integer(on_error_err_code);
                return ERR_NONE;
            }
            if (strcmp(e->call.name, "ERL") == 0 && nargs == 0) {
                *out = val_integer(on_error_err_line);
                return ERR_NONE;
            }

            error_t err = builtin_call(e->call.name, args, nargs, out);
            for (int i = 0; i < nargs; i++) val_clear(&args[i]);
            return err;
        }

        case EXPR_FIELD_ACCESS: {
            value_t *v = env_get(env, e->field.var_name);
            if (!v || v->type != VAL_RECORD) return ERR_UNDEFINED_VAR;
            type_def_t *td = &type_defs[v->rval->type_idx];
            int fi = find_field_index(td, e->field.field_name);
            if (fi < 0) return ERR_UNDEFINED_FIELD;
            *out = val_copy(&v->rval->fields[fi]);
            return ERR_NONE;
        }
    }

    return ERR_INTERNAL;
}

/* --- Statement execution --- */

/* PRINT USING format engine:
   Returns number of format chars consumed. Output goes to stdout.
   Uses snprintf with embedded precision (e.g. "%.2f") instead of %*.*f
   which has varargs issues on SLOW-32. */

/* --- Print column tracking --- */

int print_col = 0;
int print_row = 1;
int *active_print_col = &print_col;

/* Update a column counter by scanning a string (same logic as col_puts) */
static void update_col(int *col, const char *s) {
    while (*s) {
        if (*s == '\n' || *s == '\r')
            *col = 0;
        else if (*s == '\t')
            *col = (*col + 8) & ~7;
        else
            (*col)++;
        s++;
    }
}

/* Print a string to stdout and update print_col/print_row */
/* Output destination for col_puts/col_printf (default stdout) */
static FILE *col_output_fp = NULL;

static void col_puts(const char *s) {
    FILE *fp = col_output_fp ? col_output_fp : stdout;
    while (*s) {
        fputc(*s, fp);
        if (*s == '\n') {
            print_col = 0;
            print_row++;
        } else if (*s == '\r')
            print_col = 0;
        else if (*s == '\t')
            print_col = (print_col + 8) & ~7;  /* tab stops every 8 */
        else
            print_col++;
        s++;
    }
}

/* Printf wrapper that tracks print_col */
static void col_printf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    col_puts(buf);
}

static double fmt_fabs(double x) { return x < 0.0 ? -x : x; }

/* Format double into buf with exactly 'dec' decimal places, right-justified
   in 'width' characters. Returns strlen of result.
   Uses snprintf internally — handles any magnitude correctly. */
static int fmt_fixed(char *buf, int bufsz, double val, int width, int dec) {
    char tmp[512];
    char fmtstr[16];
    /* Build "%.Xf" format string (avoids %*.*f varargs issues on SLOW-32) */
    if (dec < 0) dec = 0;
    if (dec > 20) dec = 20;
    snprintf(fmtstr, sizeof(fmtstr), "%%.%df", dec);
    int len = snprintf(tmp, sizeof(tmp), fmtstr, val);
    if (len < 0) len = 0;
    if (len >= (int)sizeof(tmp)) len = (int)sizeof(tmp) - 1;
    /* Right-justify in width */
    int padding = width - len;
    if (padding < 0) padding = 0;
    int pos = 0;
    for (int i = 0; i < padding && pos < bufsz - 1; i++) buf[pos++] = ' ';
    for (int i = 0; i < len && pos < bufsz - 1; i++) buf[pos++] = tmp[i];
    buf[pos] = '\0';
    return pos;
}

/* Insert commas every 3 digits in the integer part of a formatted number.
   Works in-place on buf (must have room for expansion).
   Commas consume leading padding spaces so total width stays constant. */
static void insert_commas(char *buf, int bufsz) {
    /* Find the integer digit region: skip leading spaces/stars/signs,
       stop at '.' or end of string */
    int len = (int)strlen(buf);
    int first_digit = -1, last_digit = -1;
    int dot_pos = -1;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '.') { dot_pos = i; break; }
        if (buf[i] >= '0' && buf[i] <= '9') {
            if (first_digit < 0) first_digit = i;
            last_digit = i;
        }
    }
    if (dot_pos < 0) {
        /* No dot — digits go to end (before any trailing sign) */
        for (int i = len - 1; i >= 0; i--) {
            if (buf[i] >= '0' && buf[i] <= '9') { last_digit = i; break; }
        }
        for (int i = 0; i < len; i++) {
            if (buf[i] >= '0' && buf[i] <= '9') {
                if (first_digit < 0) first_digit = i;
                break;
            }
        }
    }
    if (first_digit < 0 || last_digit < 0) return;
    int ndigits = last_digit - first_digit + 1;
    int ncommas = (ndigits - 1) / 3;
    if (ncommas <= 0) return;

    /* Count available leading padding (spaces or stars before first digit) */
    int avail_pad = 0;
    for (int i = 0; i < first_digit; i++) {
        if (buf[i] == ' ' || buf[i] == '*') avail_pad++;
    }
    /* We can only insert as many commas as we have padding to consume */
    if (ncommas > avail_pad) ncommas = avail_pad;
    if (ncommas <= 0) return;

    /* Build new string: remove ncommas leading pad chars, insert commas */
    char tmp[512];
    int out = 0;
    /* Copy leading padding minus ncommas */
    int skip = ncommas;
    for (int i = 0; i < first_digit && out < bufsz - 1; i++) {
        if (skip > 0 && (buf[i] == ' ' || buf[i] == '*')) { skip--; continue; }
        tmp[out++] = buf[i];
    }
    /* Copy digits with commas */
    int digits_from_right = last_digit - first_digit + 1;
    for (int i = first_digit; i <= last_digit && out < bufsz - 1; i++) {
        tmp[out++] = buf[i];
        int remain = last_digit - i;
        if (remain > 0 && remain % 3 == 0 && out < bufsz - 1)
            tmp[out++] = ',';
    }
    /* Copy rest (decimal part, trailing sign, etc.) */
    for (int i = last_digit + 1; i < len && out < bufsz - 1; i++)
        tmp[out++] = buf[i];
    tmp[out] = '\0';
    /* Copy back */
    for (int i = 0; i <= out; i++) buf[i] = tmp[i];
}

static int format_using_num(const char *fmt, double val) {
    const char *start = fmt;
    int has_lead_plus = 0, has_trail_plus = 0;
    int has_trail_minus = 0;
    int has_dollar = 0, has_stars = 0;
    int has_dot = 0, has_exp = 0, has_commas = 0;
    int width = 0, decimals = 0;

    /* --- Parse phase --- */
    const char *p = fmt;
    if (*p == '+') { has_lead_plus = 1; p++; width++; }
    /* **$ must be checked before ** or $$ */
    if (p[0] == '*' && p[1] == '*' && p[2] == '$') {
        has_stars = 1; has_dollar = 1; p += 3; width += 3;
    } else if (p[0] == '$' && p[1] == '$') {
        has_dollar = 1; p += 2; width += 2;
    } else if (p[0] == '*' && p[1] == '*') {
        has_stars = 1; p += 2; width += 2;
    }
    while (*p == '#' || *p == ',') {
        if (*p == ',') has_commas = 1;
        width++; p++;
    }
    if (*p == '.') {
        has_dot = 1; p++; width++;
        while (*p == '#') { decimals++; width++; p++; }
    }
    /* Trailing sign: - or + after digit section */
    if (*p == '-') { has_trail_minus = 1; p++; width++; }
    else if (*p == '+' && !has_lead_plus) { has_trail_plus = 1; p++; width++; }
    if (p[0] == '^' && p[1] == '^' && p[2] == '^' && p[3] == '^') {
        has_exp = 1; p += 4; width += 4;
    }

    if (width == 0) return 0;

    int is_neg = (val < 0.0);
    int any_sign = has_lead_plus || has_trail_plus || has_trail_minus;
    double abs_val = fmt_fabs(val);

    char buf[512];
    if (has_exp) {
        /* Scientific notation */
        double av = abs_val;
        int exp_val = 0;
        if (av != 0.0) {
            while (av >= 10.0) { av /= 10.0; exp_val++; }
            while (av < 1.0)  { av *= 10.0; exp_val--; }
        }
        int mw = width - 4; /* subtract ^^^^ */
        if (has_trail_minus || has_trail_plus) mw--;
        if (any_sign) {
            /* Format absolute value, prepend sign */
            char tmp[512];
            fmt_fixed(tmp, sizeof(tmp), av, mw - 1, decimals);
            /* Replace first leading space with sign char */
            char sc = is_neg ? '-' : '+';
            int tlen = (int)strlen(tmp);
            int i = 0;
            while (i < tlen && tmp[i] == ' ') i++;
            if (i > 0 && has_lead_plus) { tmp[i - 1] = sc; }
            else if (has_lead_plus) {
                /* No padding — prepend */
                buf[0] = sc;
                for (int j = 0; j <= tlen; j++) buf[j + 1] = tmp[j];
                col_puts(buf);
                goto exp_part;
            }
            col_puts(tmp);
        } else {
            if (is_neg) av = -av;
            fmt_fixed(buf, sizeof(buf), av, mw, decimals);
            col_puts(buf);
        }
exp_part:
        col_printf("E%c%02d", exp_val >= 0 ? '+' : '-',
               exp_val >= 0 ? exp_val : -exp_val);
        if (has_trail_minus) { col_puts(is_neg ? "-" : " "); }
        else if (has_trail_plus) { col_puts(is_neg ? "-" : "+"); }
    } else {
        /* Fixed-point format */
        int fw = width;
        if (has_trail_minus || has_trail_plus) fw--;
        /* When sign option active, format absolute value */
        double fval = any_sign ? abs_val : val;
        /* For leading +, reserve one position for the sign character */
        if (has_lead_plus) fw--;
        fmt_fixed(buf, sizeof(buf), fval, fw, decimals);
        int blen = (int)strlen(buf);

        /* Overflow check: if formatted number (trimmed) exceeds field width */
        {
            int trimlen = blen;
            int j = 0;
            while (j < blen && buf[j] == ' ') j++;
            trimlen = blen - j;
            if (trimlen > fw) {
                /* Overflow: emit % then the raw number */
                col_puts("%");
                col_puts(buf + j);
                if (has_trail_minus) col_puts(is_neg ? "-" : " ");
                else if (has_trail_plus) col_puts(is_neg ? "-" : "+");
                return (int)(p - start);
            }
        }

        /* Insert commas if format contained them */
        if (has_commas) {
            insert_commas(buf, sizeof(buf));
            blen = (int)strlen(buf);
        }

        /* Star fill */
        if (has_stars) {
            for (int i = 0; i < blen; i++) {
                if (buf[i] == ' ') buf[i] = '*';
                else break;
            }
        }
        /* Dollar sign: place $ just before first digit */
        if (has_dollar && blen > 0) {
            int i = 0;
            while (i < blen && (buf[i] == ' ' || buf[i] == '*')) i++;
            if (i > 0) buf[i - 1] = '$';
        }
        /* Leading + sign: replace leading padding with sign */
        if (has_lead_plus) {
            char sc = is_neg ? '-' : '+';
            /* Shift buf right by 1, prepend sign */
            char tmp[512];
            tmp[0] = sc;
            for (int i = 0; i <= blen && i < (int)sizeof(tmp) - 2; i++)
                tmp[i + 1] = buf[i];
            col_puts(tmp);
        } else {
            col_puts(buf);
        }
        /* Trailing sign */
        if (has_trail_minus) { col_puts(is_neg ? "-" : " "); }
        else if (has_trail_plus) { col_puts(is_neg ? "-" : "+"); }
    }
    return (int)(p - start);
}

static int format_using_str(const char *fmt, const char *str, int slen) {
    if (*fmt == '!') {
        /* First character only */
        col_printf("%c", slen > 0 ? str[0] : ' ');
        return 1;
    }
    if (*fmt == '&') {
        /* Entire string */
        col_puts(str);
        return 1;
    }
    if (*fmt == '\\') {
        /* Fixed width: count chars between backslashes */
        const char *p = fmt + 1;
        int width = 2; /* the two backslashes count as positions */
        while (*p && *p != '\\') { width++; p++; }
        if (*p == '\\') p++;
        /* Print string padded to width */
        int printed = 0;
        for (int i = 0; i < width && i < slen; i++) { col_printf("%c", str[i]); printed++; }
        for (int i = printed; i < width; i++) col_puts(" ");
        return (int)(p - fmt);
    }
    return 0;
}

/* Execute PRINT USING — separated to reduce register pressure in exec_print */
static error_t exec_print_using_items(env_t *env, print_item_t *items, int nitems, const char *fmt_str) {
    int arg_idx = 0;
    /* Outer loop: reuse format string when args remain */
    for (;;) {
        const char *fmt = fmt_str;
        int args_this_pass = 0;
        while (*fmt) {
            if (*fmt == '#' || *fmt == '+' || *fmt == '$' || *fmt == '*') {
                /* Numeric format */
                if (arg_idx < nitems && items[arg_idx].expr) {
                    value_t v;
                    EVAL_CHECK(eval_expr(env, items[arg_idx].expr, &v));
                    double d = 0.0;
                    val_to_double(&v, &d);
                    val_clear(&v);
                    int consumed = format_using_num(fmt, d);
                    if (consumed > 0) { fmt += consumed; arg_idx++; args_this_pass++; continue; }
                }
                col_printf("%c", *fmt++);
            } else if (*fmt == '!' || *fmt == '&' || *fmt == '\\') {
                /* String format */
                if (arg_idx < nitems && items[arg_idx].expr) {
                    value_t v;
                    EVAL_CHECK(eval_expr(env, items[arg_idx].expr, &v));
                    const char *str = (v.type == VAL_STRING) ? v.sval->data : "";
                    int slen = (v.type == VAL_STRING) ? v.sval->len : 0;
                    int consumed = format_using_str(fmt, str, slen);
                    val_clear(&v);
                    if (consumed > 0) { fmt += consumed; arg_idx++; args_this_pass++; continue; }
                }
                col_printf("%c", *fmt++);
            } else if (*fmt == '_') {
                /* Literal next character */
                fmt++;
                if (*fmt) col_printf("%c", *fmt++);
            } else {
                col_printf("%c", *fmt++);
            }
        }
        /* If all args consumed, or no args consumed this pass, stop */
        if (arg_idx >= nitems || args_this_pass == 0)
            break;
        /* More args remain — print newline and restart format */
        col_puts("\n");
    }
    col_puts("\n");
    return ERR_NONE;
}

static error_t exec_print_using(env_t *env, stmt_t *s, const char *fmt_str) {
    return exec_print_using_items(env, s->print.items, s->print.nitems, fmt_str);
}

/* Evaluate a USING expression and dispatch to exec_print_using */
static error_t exec_print_using_expr(env_t *env, stmt_t *s) {
    char fmt_buf[1024];
    value_t fv;
    EVAL_CHECK(eval_expr(env, s->print.using_expr, &fv));
    if (fv.type == VAL_STRING && fv.sval)
        snprintf(fmt_buf, sizeof(fmt_buf), "%s", fv.sval->data);
    else
        fmt_buf[0] = '\0';
    val_clear(&fv);
    return exec_print_using(env, s, fmt_buf);
}

static error_t exec_print(env_t *env, stmt_t *s) {
    /* PRINT USING */
    if (s->print.using_fmt)
        return exec_print_using(env, s, s->print.using_fmt);
    if (s->print.using_expr)
        return exec_print_using_expr(env, s);

    int needs_newline = 1;

    for (int i = 0; i < s->print.nitems; i++) {
        print_item_t *item = &s->print.items[i];

        if (item->expr) {
            value_t v;
            EVAL_CHECK(eval_expr(env, item->expr, &v));

            switch (v.type) {
                case VAL_INTEGER:
                    if (v.ival >= 0) col_printf(" %d", v.ival);
                    else col_printf("%d", v.ival);
                    break;
                case VAL_DOUBLE:
                    if (v.dval >= 0) col_printf(" %g", v.dval);
                    else col_printf("%g", v.dval);
                    break;
                case VAL_STRING:
                    col_puts(v.sval->data);
                    break;
                case VAL_RECORD:
                    break;
            }
            val_clear(&v);
        }

        if (item->sep == ';') {
            needs_newline = 0;
        } else if (item->sep == ',') {
            col_puts("\t");
            needs_newline = 0;
        } else {
            needs_newline = 1;
        }
    }

    if (needs_newline)
        col_puts("\n");

    return ERR_NONE;
}

/* LPRINT — like PRINT but output to stderr */
static error_t exec_lprint(env_t *env, stmt_t *s) {
    /* LPRINT USING */
    if (s->print.using_fmt || s->print.using_expr) {
        FILE *saved_fp = col_output_fp;
        col_output_fp = stderr;
        const char *fmt;
        char fmt_buf[1024];
        if (s->print.using_fmt) {
            fmt = s->print.using_fmt;
        } else {
            value_t fv;
            error_t err = eval_expr(env, s->print.using_expr, &fv);
            if (err != ERR_NONE) { col_output_fp = saved_fp; return err; }
            if (fv.type == VAL_STRING && fv.sval)
                snprintf(fmt_buf, sizeof(fmt_buf), "%s", fv.sval->data);
            else
                fmt_buf[0] = '\0';
            val_clear(&fv);
            fmt = fmt_buf;
        }
        error_t err = exec_print_using_items(env, s->print.items, s->print.nitems, fmt);
        col_output_fp = saved_fp;
        return err;
    }

    int needs_newline = 1;

    for (int i = 0; i < s->print.nitems; i++) {
        print_item_t *item = &s->print.items[i];

        if (item->expr) {
            value_t v;
            EVAL_CHECK(eval_expr(env, item->expr, &v));

            switch (v.type) {
                case VAL_INTEGER:
                    if (v.ival >= 0) fprintf(stderr, " %d", v.ival);
                    else fprintf(stderr, "%d", v.ival);
                    break;
                case VAL_DOUBLE:
                    if (v.dval >= 0) fprintf(stderr, " %g", v.dval);
                    else fprintf(stderr, "%g", v.dval);
                    break;
                case VAL_STRING:
                    fputs(v.sval->data, stderr);
                    break;
                case VAL_RECORD:
                    break;
            }
            val_clear(&v);
        }

        if (item->sep == ';') {
            needs_newline = 0;
        } else if (item->sep == ',') {
            fputs("\t", stderr);
            needs_newline = 0;
        } else {
            needs_newline = 1;
        }
    }

    if (needs_newline)
        fputs("\n", stderr);

    return ERR_NONE;
}

static error_t exec_input(env_t *env, stmt_t *s) {
    if (s->input.prompt)
        col_puts(s->input.prompt);
    else
        col_puts("? ");

    char line[1024];
    int pos = 0;
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n') {
        if (pos < 1023)
            line[pos++] = (char)ch;
    }
    line[pos] = '\0';

    print_col = 0; /* newline consumed by getchar */

    if (ch == EOF && pos == 0)
        return ERR_INPUT_PAST_END;

    const char *p = line;
    for (int i = 0; i < s->input.nvars; i++) {
        if (env_is_const(env, s->input.varnames[i]))
            return ERR_CONST_REASSIGN;
        while (*p == ' ') p++;

        if (s->input.vartypes[i] == VAL_STRING) {
            const char *start = p;
            while (*p && *p != ',') p++;
            int len = (int)(p - start);
            while (len > 0 && start[len - 1] == ' ') len--;
            value_t v = val_string(start, len);
            env_set(env, s->input.varnames[i], &v);
            val_clear(&v);
        } else {
            char *endp;
            double d = strtod(p, &endp);
            if (endp == p) d = 0.0;
            p = endp;
            if (s->input.vartypes[i] == VAL_INTEGER) {
                value_t v = val_integer((int)d);
                env_set(env, s->input.varnames[i], &v);
            } else {
                value_t v = val_double(d);
                env_set(env, s->input.varnames[i], &v);
            }
        }

        if (*p == ',') p++;
    }

    return ERR_NONE;
}

static error_t exec_cls(stmt_t *s) {
    (void)s;
    sb_term_cls();
    print_col = 0;
    print_row = 1;
    return ERR_NONE;
}

static error_t exec_locate(env_t *env, stmt_t *s) {
    value_t rv, cv;
    EVAL_CHECK(eval_expr(env, s->locate_stmt.row, &rv));
    error_t err = eval_expr(env, s->locate_stmt.col, &cv);
    if (err != ERR_NONE) {
        val_clear(&rv);
        return err;
    }
    int row, col;
    err = val_to_integer(&rv, &row);
    if (err == ERR_NONE) err = val_to_integer(&cv, &col);
    val_clear(&rv);
    val_clear(&cv);
    if (err != ERR_NONE) return err;
    if (row < 1 || col < 1) return ERR_ILLEGAL_FUNCTION_CALL;
    sb_term_locate(row, col);
    print_col = col - 1;
    print_row = row;
    return ERR_NONE;
}

static error_t exec_color(env_t *env, stmt_t *s) {
    int fg = 7;
    int bg = 0;
    if (s->color_stmt.has_fg) {
        value_t fv;
        EVAL_CHECK(eval_expr(env, s->color_stmt.fg, &fv));
        error_t err = val_to_integer(&fv, &fg);
        val_clear(&fv);
        if (err != ERR_NONE) return err;
        if (fg < 0 || fg > 15) return ERR_ILLEGAL_FUNCTION_CALL;
    }
    if (s->color_stmt.has_bg) {
        value_t bv;
        EVAL_CHECK(eval_expr(env, s->color_stmt.bg, &bv));
        error_t err = val_to_integer(&bv, &bg);
        val_clear(&bv);
        if (err != ERR_NONE) return err;
        if (bg < 0 || bg > 15) return ERR_ILLEGAL_FUNCTION_CALL;
    }
    sb_term_color(fg, bg, s->color_stmt.has_fg, s->color_stmt.has_bg);
    return ERR_NONE;
}

/* Resolve an lvalue expression to a pointer to its storage.
   Supports: simple variable, array element, record field. */
static error_t resolve_lvalue(env_t *env, expr_t *e, value_t **out) {
    switch (e->type) {
    case EXPR_VARIABLE: {
        value_t *v = env_get(env, e->var.name);
        if (!v) {
            /* Auto-create with default type */
            value_t def = {.type = e->var.var_type};
            env_set(env, e->var.name, &def);
            v = env_get(env, e->var.name);
        }
        *out = v;
        return ERR_NONE;
    }
    case EXPR_CALL: {
        /* Array element access */
        int indices[MAX_ARRAY_DIMS];
        for (int i = 0; i < e->call.nargs; i++) {
            value_t iv;
            error_t err = eval_expr(env, e->call.args[i], &iv);
            if (err != ERR_NONE) return err;
            err = val_to_integer(&iv, &indices[i]);
            val_clear(&iv);
            if (err != ERR_NONE) return err;
        }
        return array_get(e->call.name, indices, e->call.nargs, out);
    }
    case EXPR_FIELD_ACCESS: {
        value_t *v = env_get(env, e->field.var_name);
        if (!v || v->type != VAL_RECORD) return ERR_UNDEFINED_VAR;
        type_def_t *td = &type_defs[v->rval->type_idx];
        int fi = find_field_index(td, e->field.field_name);
        if (fi < 0) return ERR_UNDEFINED_FIELD;
        *out = &v->rval->fields[fi];
        return ERR_NONE;
    }
    default:
        return ERR_TYPE_MISMATCH;
    }
}

/* Check if an lvalue expression refers to a CONST variable */
static int lvalue_is_const(env_t *env, expr_t *e) {
    if (e->type == EXPR_VARIABLE)
        return env_is_const(env, e->var.name);
    if (e->type == EXPR_FIELD_ACCESS)
        return env_is_const(env, e->field.var_name);
    return 0;
}

static error_t exec_assign(env_t *env, stmt_t *s) {
    if (env_is_const(env, s->assign.name))
        return ERR_CONST_REASSIGN;

    /* OPTION EXPLICIT: variable must already exist */
    if (option_explicit && !env_exists(env, s->assign.name))
        return ERR_UNDEFINED_VAR;

    value_t v;
    EVAL_CHECK(eval_expr(env, s->assign.value, &v));

    val_type_t vt = resolve_var_type(s->assign.name);
    if (vt == VAL_INTEGER && v.type != VAL_INTEGER) {
        int iv;
        error_t err = val_to_integer(&v, &iv);
        val_clear(&v);
        if (err != ERR_NONE) return err;
        v = val_integer(iv);
    } else if (vt == VAL_STRING && v.type != VAL_STRING) {
        char buf[256];
        error_t err = val_to_string(&v, buf, sizeof(buf));
        val_clear(&v);
        if (err != ERR_NONE) return err;
        v = val_string_cstr(buf);
    }

    env_set(env, s->assign.name, &v);
    val_clear(&v);
    return ERR_NONE;
}

static error_t exec_if(env_t *env, stmt_t *s) {
    value_t cond;
    EVAL_CHECK(eval_expr(env, s->if_stmt.condition, &cond));
    int is_true = val_is_true(&cond);
    val_clear(&cond);

    if (is_true) {
        if (s->if_stmt.then_body)
            return eval_stmts(env, s->if_stmt.then_body);
    } else {
        if (s->if_stmt.else_body)
            return eval_stmts(env, s->if_stmt.else_body);
    }
    return ERR_NONE;
}

static error_t exec_for(env_t *env, stmt_t *s) {
    if (env_is_const(env, s->for_stmt.var_name))
        return ERR_CONST_REASSIGN;

    value_t start_val, end_val, step_val;
    EVAL_CHECK(eval_expr(env, s->for_stmt.start, &start_val));

    error_t err = eval_expr(env, s->for_stmt.end, &end_val);
    if (err != ERR_NONE) { val_clear(&start_val); return err; }

    if (s->for_stmt.step) {
        err = eval_expr(env, s->for_stmt.step, &step_val);
        if (err != ERR_NONE) {
            val_clear(&start_val);
            val_clear(&end_val);
            return err;
        }
    } else {
        step_val = val_integer(1);
    }

    double counter, limit, step;
    error_t cerr = val_to_double(&start_val, &counter);
    if (cerr == ERR_NONE) cerr = val_to_double(&end_val, &limit);
    if (cerr == ERR_NONE) cerr = val_to_double(&step_val, &step);
    val_clear(&start_val);
    val_clear(&end_val);
    val_clear(&step_val);
    if (cerr != ERR_NONE) return cerr;

    if (step == 0.0) return ERR_ILLEGAL_FUNCTION_CALL;

    while (1) {
        if (step > 0 && counter > limit) break;
        if (step < 0 && counter < limit) break;

        if (s->for_stmt.var_type == VAL_INTEGER) {
            value_t v = val_integer((int)counter);
            env_set(env, s->for_stmt.var_name, &v);
        } else {
            value_t v = val_double(counter);
            env_set(env, s->for_stmt.var_name, &v);
        }

        if (s->for_stmt.body) {
            err = eval_stmts(env, s->for_stmt.body);
            if (err == ERR_BREAK || err == ERR_EXIT_FOR) break;
            if (err != ERR_NONE) return err;
        }

        counter += step;
    }

    return ERR_NONE;
}

static error_t exec_while(env_t *env, stmt_t *s) {
    while (1) {
        value_t cond;
        EVAL_CHECK(eval_expr(env, s->while_stmt.condition, &cond));
        int is_true = val_is_true(&cond);
        val_clear(&cond);
        if (!is_true) break;

        if (s->while_stmt.body) {
            error_t err = eval_stmts(env, s->while_stmt.body);
            if (err == ERR_BREAK || err == ERR_EXIT_WHILE) break;
            if (err != ERR_NONE) return err;
        }
    }
    return ERR_NONE;
}

static error_t exec_do_loop(env_t *env, stmt_t *s) {
    while (1) {
        if (s->do_loop.pre_cond) {
            value_t cond;
            EVAL_CHECK(eval_expr(env, s->do_loop.pre_cond, &cond));
            int is_true = val_is_true(&cond);
            val_clear(&cond);
            if (s->do_loop.pre_until) is_true = !is_true;
            if (!is_true) break;
        }

        if (s->do_loop.body) {
            error_t err = eval_stmts(env, s->do_loop.body);
            if (err == ERR_EXIT_DO) break;
            if (err != ERR_NONE) return err;
        }

        if (s->do_loop.post_cond) {
            value_t cond;
            EVAL_CHECK(eval_expr(env, s->do_loop.post_cond, &cond));
            int is_true = val_is_true(&cond);
            val_clear(&cond);
            if (s->do_loop.post_until) is_true = !is_true;
            if (!is_true) break;
        }
    }
    return ERR_NONE;
}

static error_t exec_select(env_t *env, stmt_t *s) {
    value_t test;
    EVAL_CHECK(eval_expr(env, s->select_stmt.test_expr, &test));

    case_clause_t *matched = NULL;
    case_clause_t *else_clause = NULL;

    for (case_clause_t *c = s->select_stmt.clauses; c; c = c->next) {
        if (c->is_else) {
            else_clause = c;
            continue;
        }

        int found = 0;
        for (int i = 0; i < c->nmatches && !found; i++) {
            case_match_t *m = &c->matches[i];
            if (m->match_type == 0) {
                value_t v;
                error_t err = eval_expr(env, m->expr1, &v);
                if (err != ERR_NONE) { val_clear(&test); return err; }
                int cmp;
                err = val_compare(&test, &v, &cmp);
                val_clear(&v);
                if (err != ERR_NONE) { val_clear(&test); return err; }
                if (cmp == 0) found = 1;
            } else if (m->match_type == 1) {
                value_t lo, hi;
                error_t err = eval_expr(env, m->expr1, &lo);
                if (err != ERR_NONE) { val_clear(&test); return err; }
                err = eval_expr(env, m->expr2, &hi);
                if (err != ERR_NONE) { val_clear(&lo); val_clear(&test); return err; }
                int cmp_lo, cmp_hi;
                err = val_compare(&test, &lo, &cmp_lo);
                if (err == ERR_NONE)
                    err = val_compare(&test, &hi, &cmp_hi);
                val_clear(&lo);
                val_clear(&hi);
                if (err != ERR_NONE) { val_clear(&test); return err; }
                if (cmp_lo >= 0 && cmp_hi <= 0) found = 1;
            } else if (m->match_type == 2) {
                value_t v;
                error_t err = eval_expr(env, m->expr1, &v);
                if (err != ERR_NONE) { val_clear(&test); return err; }
                int cmp;
                err = val_compare(&test, &v, &cmp);
                val_clear(&v);
                if (err != ERR_NONE) { val_clear(&test); return err; }
                switch (m->is_op) {
                    case CMP_EQ: found = (cmp == 0); break;
                    case CMP_NE: found = (cmp != 0); break;
                    case CMP_LT: found = (cmp < 0); break;
                    case CMP_GT: found = (cmp > 0); break;
                    case CMP_LE: found = (cmp <= 0); break;
                    case CMP_GE: found = (cmp >= 0); break;
                }
            }
        }

        if (found) {
            matched = c;
            break;
        }
    }

    val_clear(&test);
    if (!matched) matched = else_clause;
    if (matched && matched->body)
        return eval_stmts(env, matched->body);
    return ERR_NONE;
}

static error_t exec_const(env_t *env, stmt_t *s) {
    value_t v;
    EVAL_CHECK(eval_expr(env, s->const_stmt.value, &v));

    if (s->const_stmt.var_type == VAL_INTEGER && v.type != VAL_INTEGER) {
        int iv;
        error_t err = val_to_integer(&v, &iv);
        val_clear(&v);
        if (err != ERR_NONE) return err;
        v = val_integer(iv);
    } else if (s->const_stmt.var_type == VAL_STRING && v.type != VAL_STRING) {
        char buf[256];
        error_t err = val_to_string(&v, buf, sizeof(buf));
        val_clear(&v);
        if (err != ERR_NONE) return err;
        v = val_string_cstr(buf);
    }

    env_set_const(env, s->const_stmt.name, &v);
    val_clear(&v);
    return ERR_NONE;
}

static error_t exec_call_stmt(env_t *env, stmt_t *s) {
    proc_entry_t *proc = find_proc(s->call_stmt.name);
    if (!proc)
        return ERR_UNDEFINED_PROC;

    if (proc->def->type == STMT_FUNC_DEF) {
        value_t result;
        error_t err = call_proc(env, proc, s->call_stmt.args,
                                s->call_stmt.nargs, &result);
        if (err == ERR_NONE) val_clear(&result);
        return err;
    }

    return call_proc(env, proc, s->call_stmt.args,
                     s->call_stmt.nargs, NULL);
}

/* --- Stage 3 statement handlers --- */

static error_t exec_dim(env_t *env, stmt_t *s) {
    int sizes[MAX_ARRAY_DIMS];
    for (int i = 0; i < s->dim_stmt.ndims; i++) {
        value_t dv;
        EVAL_CHECK(eval_expr(env, s->dim_stmt.dims[i], &dv));
        int upper;
        error_t err = val_to_integer(&dv, &upper);
        val_clear(&dv);
        if (err != ERR_NONE) return err;
        sizes[i] = upper - option_base + 1;
        if (sizes[i] <= 0) return ERR_ILLEGAL_FUNCTION_CALL;
    }

    if (s->dim_stmt.is_redim)
        return array_redim(s->dim_stmt.name, s->dim_stmt.elem_type,
                           sizes, s->dim_stmt.ndims, option_base,
                           s->dim_stmt.preserve);
    else
        return array_dim(s->dim_stmt.name, s->dim_stmt.elem_type,
                         sizes, s->dim_stmt.ndims, option_base);
}

static error_t exec_array_assign(env_t *env, stmt_t *s) {
    int indices[MAX_ARRAY_DIMS];
    for (int i = 0; i < s->array_assign.nindices; i++) {
        value_t iv;
        EVAL_CHECK(eval_expr(env, s->array_assign.indices[i], &iv));
        error_t err = val_to_integer(&iv, &indices[i]);
        val_clear(&iv);
        if (err != ERR_NONE) return err;
    }

    value_t v;
    EVAL_CHECK(eval_expr(env, s->array_assign.value, &v));

    /* Coerce to array element type */
    sb_array_t *arr = array_find(s->array_assign.name);
    if (!arr) {
        val_clear(&v);
        return ERR_UNDEFINED_VAR;
    }

    if (arr->elem_type == VAL_INTEGER && v.type != VAL_INTEGER) {
        int iv;
        error_t err = val_to_integer(&v, &iv);
        val_clear(&v);
        if (err != ERR_NONE) return err;
        v = val_integer(iv);
    } else if (arr->elem_type == VAL_STRING && v.type != VAL_STRING) {
        char buf[256];
        error_t err = val_to_string(&v, buf, sizeof(buf));
        val_clear(&v);
        if (err != ERR_NONE) return err;
        v = val_string_cstr(buf);
    }

    error_t err = array_set(s->array_assign.name, indices,
                            s->array_assign.nindices, &v);
    val_clear(&v);
    return err;
}

static error_t exec_erase(env_t *env, stmt_t *s) {
    for (int i = 0; i < s->erase_stmt.nnames; i++)
        array_erase(s->erase_stmt.names[i]);
    return ERR_NONE;
}

static error_t exec_read(env_t *env, stmt_t *s) {
    for (int i = 0; i < s->read_stmt.nvars; i++) {
        if (env_is_const(env, s->read_stmt.varnames[i]))
            return ERR_CONST_REASSIGN;
        if (data_read_ptr >= data_pool_count)
            return ERR_OUT_OF_DATA;

        value_t v = val_copy(&data_pool[data_read_ptr++]);

        if (s->read_stmt.vartypes[i] == VAL_INTEGER && v.type != VAL_INTEGER) {
            int iv;
            error_t err = val_to_integer(&v, &iv);
            val_clear(&v);
            if (err != ERR_NONE) return err;
            v = val_integer(iv);
        } else if (s->read_stmt.vartypes[i] == VAL_STRING && v.type != VAL_STRING) {
            char buf[256];
            error_t err = val_to_string(&v, buf, sizeof(buf));
            val_clear(&v);
            if (err != ERR_NONE) return err;
            v = val_string_cstr(buf);
        }

        env_set(env, s->read_stmt.varnames[i], &v);
        val_clear(&v);
    }
    return ERR_NONE;
}

static error_t exec_restore(stmt_t *s) {
    if (s->restore_stmt.label[0] == '\0') {
        data_read_ptr = 0;
    } else {
        int offset = find_label_data_offset(s->restore_stmt.label);
        if (offset < 0) return ERR_UNDEFINED_LABEL;
        data_read_ptr = offset;
    }
    return ERR_NONE;
}

/* --- Stage 5: File I/O handlers --- */

static error_t exec_open(env_t *env, stmt_t *s) {
    value_t fname;
    EVAL_CHECK(eval_expr(env, s->open_stmt.filename, &fname));
    if (fname.type != VAL_STRING) { val_clear(&fname); return ERR_TYPE_MISMATCH; }
    value_t hv;
    error_t err = eval_expr(env, s->open_stmt.handle_num, &hv);
    if (err != ERR_NONE) { val_clear(&fname); return err; }
    int handle;
    err = val_to_integer(&hv, &handle);
    val_clear(&hv);
    if (err != ERR_NONE) { val_clear(&fname); return err; }
    err = fileio_open(fname.sval->data, (file_mode_t)s->open_stmt.mode, handle);
    val_clear(&fname);
    if (err != ERR_NONE) return err;

    /* Set record length for RANDOM mode */
    if (s->open_stmt.mode == FMODE_RANDOM) {
        int reclen = 128; /* QBasic default */
        if (s->open_stmt.reclen) {
            value_t rv;
            err = eval_expr(env, s->open_stmt.reclen, &rv);
            if (err != ERR_NONE) return err;
            err = val_to_integer(&rv, &reclen);
            val_clear(&rv);
            if (err != ERR_NONE) return err;
        }
        fileio_set_reclen(handle, reclen);
    }
    return ERR_NONE;
}

static error_t exec_close(env_t *env, stmt_t *s) {
    if (!s->close_stmt.handle_num)
        return fileio_close(0);
    value_t hv;
    EVAL_CHECK(eval_expr(env, s->close_stmt.handle_num, &hv));
    int handle;
    error_t err = val_to_integer(&hv, &handle);
    val_clear(&hv);
    if (err != ERR_NONE) return err;
    return fileio_close(handle);
}

static error_t exec_print_file(env_t *env, stmt_t *s) {
    value_t hv;
    EVAL_CHECK(eval_expr(env, s->print_file.handle_num, &hv));
    int handle;
    error_t err = val_to_integer(&hv, &handle);
    val_clear(&hv);
    if (err != ERR_NONE) return err;
    FILE *fp = fileio_get(handle);
    if (!fp) return ERR_FILE_NOT_OPEN;

    /* Redirect active_print_col to this file's column counter */
    int *file_col = fileio_get_col_ptr(handle);
    int *saved_col = active_print_col;
    if (file_col) active_print_col = file_col;

    /* PRINT #n, USING */
    if (s->print_file.using_fmt || s->print_file.using_expr) {
        FILE *saved_fp = col_output_fp;
        col_output_fp = fp;
        const char *fmt;
        char fmt_buf[1024];
        if (s->print_file.using_fmt) {
            fmt = s->print_file.using_fmt;
        } else {
            value_t fv;
            err = eval_expr(env, s->print_file.using_expr, &fv);
            if (err != ERR_NONE) { col_output_fp = saved_fp; active_print_col = saved_col; return err; }
            if (fv.type == VAL_STRING && fv.sval)
                snprintf(fmt_buf, sizeof(fmt_buf), "%s", fv.sval->data);
            else
                fmt_buf[0] = '\0';
            val_clear(&fv);
            fmt = fmt_buf;
        }
        err = exec_print_using_items(env, s->print_file.items, s->print_file.nitems, fmt);
        col_output_fp = saved_fp;
        active_print_col = saved_col;
        return err;
    }

    char buf[1024];
    int needs_newline = 1;
    for (int i = 0; i < s->print_file.nitems; i++) {
        print_item_t *item = &s->print_file.items[i];
        if (item->expr) {
            value_t v;
            err = eval_expr(env, item->expr, &v);
            if (err != ERR_NONE) { active_print_col = saved_col; return err; }
            switch (v.type) {
                case VAL_INTEGER:
                    if (v.ival >= 0) snprintf(buf, sizeof(buf), " %d", v.ival);
                    else snprintf(buf, sizeof(buf), "%d", v.ival);
                    fprintf(fp, "%s", buf);
                    update_col(active_print_col, buf);
                    break;
                case VAL_DOUBLE:
                    if (v.dval >= 0) snprintf(buf, sizeof(buf), " %g", v.dval);
                    else snprintf(buf, sizeof(buf), "%g", v.dval);
                    fprintf(fp, "%s", buf);
                    update_col(active_print_col, buf);
                    break;
                case VAL_STRING:
                    fprintf(fp, "%s", v.sval->data);
                    update_col(active_print_col, v.sval->data);
                    break;
                case VAL_RECORD:
                    break;
            }
            val_clear(&v);
        }
        if (item->sep == ';') {
            needs_newline = 0;
        } else if (item->sep == ',') {
            fprintf(fp, "\t");
            update_col(active_print_col, "\t");
            needs_newline = 0;
        } else {
            needs_newline = 1;
        }
    }
    if (needs_newline) {
        fprintf(fp, "\n");
        update_col(active_print_col, "\n");
    }

    active_print_col = saved_col;
    return ERR_NONE;
}

static error_t exec_write_file(env_t *env, stmt_t *s) {
    value_t hv;
    EVAL_CHECK(eval_expr(env, s->print_file.handle_num, &hv));
    int handle;
    error_t err = val_to_integer(&hv, &handle);
    val_clear(&hv);
    if (err != ERR_NONE) return err;
    FILE *fp = fileio_get(handle);
    if (!fp) return ERR_FILE_NOT_OPEN;

    for (int i = 0; i < s->print_file.nitems; i++) {
        if (i > 0) fprintf(fp, ",");
        print_item_t *item = &s->print_file.items[i];
        if (item->expr) {
            value_t v;
            EVAL_CHECK(eval_expr(env, item->expr, &v));
            switch (v.type) {
                case VAL_INTEGER: fprintf(fp, "%d", v.ival); break;
                case VAL_DOUBLE: fprintf(fp, "%g", v.dval); break;
                case VAL_STRING: fprintf(fp, "\"%s\"", v.sval->data); break;
                case VAL_RECORD: break;
            }
            val_clear(&v);
        }
    }
    fprintf(fp, "\n");
    return ERR_NONE;
}

static error_t exec_input_file(env_t *env, stmt_t *s) {
    value_t hv;
    EVAL_CHECK(eval_expr(env, s->input_file.handle_num, &hv));
    int handle;
    error_t err = val_to_integer(&hv, &handle);
    val_clear(&hv);
    if (err != ERR_NONE) return err;
    FILE *fp = fileio_get(handle);
    if (!fp) return ERR_FILE_NOT_OPEN;

    for (int i = 0; i < s->input_file.nvars; i++) {
        if (env_is_const(env, s->input_file.varnames[i]))
            return ERR_CONST_REASSIGN;
        /* Read value from file: skip whitespace, read until comma or newline */
        char buf[1024];
        int pos = 0;
        int ch;
        /* Skip leading whitespace */
        while ((ch = fgetc(fp)) != -1 && (ch == ' ' || ch == '\t'));
        if (ch == -1) return ERR_INPUT_PAST_END;

        int quoted = 0;
        if (ch == '"') { quoted = 1; }
        else if (ch != ',' && ch != '\n' && ch != '\r') {
            buf[pos++] = (char)ch;
        }

        if (quoted) {
            while ((ch = fgetc(fp)) != -1 && ch != '"' && pos < 1023)
                buf[pos++] = (char)ch;
            if (ch == '"') { /* skip closing quote */
                ch = fgetc(fp); /* eat comma or newline after quote */
            }
        } else {
            while ((ch = fgetc(fp)) != -1 && ch != ',' && ch != '\n' && ch != '\r' && pos < 1023)
                buf[pos++] = (char)ch;
        }
        buf[pos] = '\0';

        /* Assign to variable */
        val_type_t vtype = s->input_file.vartypes[i];
        value_t val;
        if (vtype == VAL_STRING) {
            val = val_string_cstr(buf);
        } else if (vtype == VAL_INTEGER) {
            val = val_integer(atoi(buf));
        } else {
            val = val_double(atof(buf));
        }
        env_set(env, s->input_file.varnames[i], &val);
        val_clear(&val);
    }
    return ERR_NONE;
}

static error_t exec_line_input(env_t *env, stmt_t *s) {
    value_t hv;
    EVAL_CHECK(eval_expr(env, s->input_file.handle_num, &hv));
    int handle;
    error_t err = val_to_integer(&hv, &handle);
    val_clear(&hv);
    if (err != ERR_NONE) return err;
    FILE *fp = fileio_get(handle);
    if (!fp) return ERR_FILE_NOT_OPEN;

    char buf[1024];
    int pos = 0;
    int ch;
    while ((ch = fgetc(fp)) != -1 && ch != '\n' && pos < 1023)
        buf[pos++] = (char)ch;
    buf[pos] = '\0';
    if (pos == 0 && ch == -1) return ERR_INPUT_PAST_END;

    if (s->input_file.nvars > 0) {
        if (env_is_const(env, s->input_file.varnames[0]))
            return ERR_CONST_REASSIGN;
        value_t val = val_string_cstr(buf);
        env_set(env, s->input_file.varnames[0], &val);
        val_clear(&val);
    }
    return ERR_NONE;
}

/* LINE INPUT (console) — read full line without comma/quote parsing */
static error_t exec_line_input_console(env_t *env, stmt_t *s) {
    if (s->input.prompt)
        col_puts(s->input.prompt);

    char line[1024];
    int pos = 0;
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n') {
        if (pos < 1023)
            line[pos++] = (char)ch;
    }
    line[pos] = '\0';
    print_col = 0;

    if (ch == EOF && pos == 0)
        return ERR_INPUT_PAST_END;

    if (s->input.nvars > 0) {
        if (env_is_const(env, s->input.varnames[0]))
            return ERR_CONST_REASSIGN;
        value_t val = val_string_cstr(line);
        env_set(env, s->input.varnames[0], &val);
        val_clear(&val);
    }
    return ERR_NONE;
}

/* WRITE (console) — comma-delimited, quoted string output */
static error_t exec_write_console(env_t *env, stmt_t *s) {
    for (int i = 0; i < s->print.nitems; i++) {
        if (i > 0) col_puts(",");
        print_item_t *item = &s->print.items[i];
        if (item->expr) {
            value_t v;
            EVAL_CHECK(eval_expr(env, item->expr, &v));
            switch (v.type) {
                case VAL_INTEGER: {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%d", v.ival);
                    col_puts(buf);
                    break;
                }
                case VAL_DOUBLE: {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%g", v.dval);
                    col_puts(buf);
                    break;
                }
                case VAL_STRING: {
                    col_puts("\"");
                    col_puts(v.sval ? v.sval->data : "");
                    col_puts("\"");
                    break;
                }
                case VAL_RECORD: break;
            }
            val_clear(&v);
        }
    }
    col_puts("\n");
    return ERR_NONE;
}

static error_t exec_kill(env_t *env, stmt_t *s) {
    value_t fname;
    EVAL_CHECK(eval_expr(env, s->kill_stmt.filename, &fname));
    if (fname.type != VAL_STRING) { val_clear(&fname); return ERR_TYPE_MISMATCH; }
    error_t err = fileio_kill(fname.sval->data);
    val_clear(&fname);
    return err;
}

static error_t exec_name(env_t *env, stmt_t *s) {
    value_t old_v, new_v;
    EVAL_CHECK(eval_expr(env, s->name_stmt.oldname, &old_v));
    error_t err = eval_expr(env, s->name_stmt.newname, &new_v);
    if (err != ERR_NONE) { val_clear(&old_v); return err; }
    if (old_v.type != VAL_STRING || new_v.type != VAL_STRING) {
        val_clear(&old_v); val_clear(&new_v); return ERR_TYPE_MISMATCH;
    }
    err = fileio_rename(old_v.sval->data, new_v.sval->data);
    val_clear(&old_v); val_clear(&new_v);
    return err;
}

/* --- Binary I/O: GET, PUT, SEEK --- */

static error_t exec_get(env_t *env, stmt_t *s) {
    value_t hv;
    EVAL_CHECK(eval_expr(env, s->get_put_stmt.handle_num, &hv));
    int handle;
    error_t err = val_to_integer(&hv, &handle);
    val_clear(&hv);
    if (err != ERR_NONE) return err;

    FILE *fp = fileio_get(handle);
    if (!fp) return ERR_FILE_NOT_OPEN;
    file_mode_t mode = fileio_get_mode(handle);
    if (mode != FMODE_BINARY && mode != FMODE_RANDOM)
        return ERR_ILLEGAL_FUNCTION_CALL;

    /* Seek to position if given */
    if (s->get_put_stmt.position) {
        value_t pv;
        EVAL_CHECK(eval_expr(env, s->get_put_stmt.position, &pv));
        int pos;
        err = val_to_integer(&pv, &pos);
        val_clear(&pv);
        if (err != ERR_NONE) return err;
        if (pos < 1) return ERR_ILLEGAL_FUNCTION_CALL;
        if (mode == FMODE_RANDOM) {
            int reclen = fileio_get_reclen(handle);
            if (reclen <= 0) reclen = 128;
            fseek(fp, (long)(pos - 1) * reclen, SEEK_SET);
        } else {
            fseek(fp, (long)(pos - 1), SEEK_SET);
        }
    }

    /* Read based on variable type */
    val_type_t vt = s->get_put_stmt.vartype;
    value_t result;
    if (vt == VAL_INTEGER) {
        int ival = 0;
        fread(&ival, 4, 1, fp);
        result = val_integer(ival);
    } else if (vt == VAL_DOUBLE) {
        double dval = 0.0;
        fread(&dval, 8, 1, fp);
        result = val_double(dval);
    } else if (vt == VAL_STRING) {
        /* String GET: read LEN(var$) bytes */
        value_t *sv = env_get(env, s->get_put_stmt.varname);
        int len = 0;
        if (sv && sv->type == VAL_STRING) len = sv->sval->len;
        if (len <= 0) len = 1;
        char *buf = malloc(len + 1);
        if (!buf) return ERR_OUT_OF_MEMORY;
        int got = fread(buf, 1, len, fp);
        buf[got] = '\0';
        result = val_string(buf, got);
        free(buf);
    } else {
        return ERR_TYPE_MISMATCH;
    }
    env_set(env, s->get_put_stmt.varname, &result);
    val_clear(&result);
    return ERR_NONE;
}

static error_t exec_put(env_t *env, stmt_t *s) {
    value_t hv;
    EVAL_CHECK(eval_expr(env, s->get_put_stmt.handle_num, &hv));
    int handle;
    error_t err = val_to_integer(&hv, &handle);
    val_clear(&hv);
    if (err != ERR_NONE) return err;

    FILE *fp = fileio_get(handle);
    if (!fp) return ERR_FILE_NOT_OPEN;
    file_mode_t mode = fileio_get_mode(handle);
    if (mode != FMODE_BINARY && mode != FMODE_RANDOM)
        return ERR_ILLEGAL_FUNCTION_CALL;

    /* Seek to position if given */
    if (s->get_put_stmt.position) {
        value_t pv;
        EVAL_CHECK(eval_expr(env, s->get_put_stmt.position, &pv));
        int pos;
        err = val_to_integer(&pv, &pos);
        val_clear(&pv);
        if (err != ERR_NONE) return err;
        if (pos < 1) return ERR_ILLEGAL_FUNCTION_CALL;
        if (mode == FMODE_RANDOM) {
            int reclen = fileio_get_reclen(handle);
            if (reclen <= 0) reclen = 128;
            fseek(fp, (long)(pos - 1) * reclen, SEEK_SET);
        } else {
            fseek(fp, (long)(pos - 1), SEEK_SET);
        }
    }

    /* Write based on variable type */
    value_t *v = env_get(env, s->get_put_stmt.varname);
    if (!v) return ERR_UNDEFINED_VAR;

    if (v->type == VAL_INTEGER) {
        int ival = v->ival;
        fwrite(&ival, 4, 1, fp);
    } else if (v->type == VAL_DOUBLE) {
        double dval = v->dval;
        fwrite(&dval, 8, 1, fp);
    } else if (v->type == VAL_STRING) {
        int len = v->sval->len;
        fwrite(v->sval->data, 1, len, fp);
    } else {
        return ERR_TYPE_MISMATCH;
    }
    fflush(fp);
    return ERR_NONE;
}

static error_t exec_seek(env_t *env, stmt_t *s) {
    value_t hv;
    EVAL_CHECK(eval_expr(env, s->seek_stmt.seek_handle, &hv));
    int handle;
    error_t err = val_to_integer(&hv, &handle);
    val_clear(&hv);
    if (err != ERR_NONE) return err;

    FILE *fp = fileio_get(handle);
    if (!fp) return ERR_FILE_NOT_OPEN;

    value_t pv;
    EVAL_CHECK(eval_expr(env, s->seek_stmt.seek_position, &pv));
    int pos;
    err = val_to_integer(&pv, &pos);
    val_clear(&pv);
    if (err != ERR_NONE) return err;
    if (pos < 1) return ERR_ILLEGAL_FUNCTION_CALL;

    file_mode_t mode = fileio_get_mode(handle);
    if (mode == FMODE_RANDOM) {
        int reclen = fileio_get_reclen(handle);
        if (reclen <= 0) reclen = 128;
        fseek(fp, (long)(pos - 1) * reclen, SEEK_SET);
    } else {
        fseek(fp, (long)(pos - 1), SEEK_SET);
    }
    return ERR_NONE;
}

/* --- Stage 6 statement handlers --- */

static error_t exec_type_def(stmt_t *s) {
    if (type_def_count >= MAX_TYPE_DEFS)
        return ERR_OUT_OF_MEMORY;
    int idx = type_def_count++;
    strncpy(type_defs[idx].name, s->type_def.name, 63);
    type_defs[idx].nfields = s->type_def.nfields;
    for (int i = 0; i < s->type_def.nfields; i++) {
        strncpy(type_defs[idx].field_names[i],
                s->type_def.field_names[i], 63);
        type_defs[idx].field_types[i] = s->type_def.field_types[i];
    }
    return ERR_NONE;
}

static error_t exec_dim_as_type(env_t *env, stmt_t *s) {
    int ti = find_type_def(s->dim_as_type.type_name);
    if (ti < 0) return ERR_UNDEFINED_TYPE;
    type_def_t *td = &type_defs[ti];
    sb_record_t *rec = record_alloc(ti, td->nfields);
    if (!rec) return ERR_OUT_OF_MEMORY;
    for (int i = 0; i < td->nfields; i++) {
        val_clear(&rec->fields[i]);
        rec->fields[i] = val_default(td->field_types[i]);
    }
    value_t val;
    val.type = VAL_RECORD;
    val.rval = rec;
    env_set(env, s->dim_as_type.name, &val);
    val_clear(&val);
    return ERR_NONE;
}

static error_t exec_field_assign(env_t *env, stmt_t *s) {
    if (env_is_const(env, s->field_assign.var_name))
        return ERR_CONST_REASSIGN;
    value_t *inst = env_get(env, s->field_assign.var_name);
    if (!inst || inst->type != VAL_RECORD) return ERR_UNDEFINED_VAR;
    type_def_t *td = &type_defs[inst->rval->type_idx];
    int fi = find_field_index(td, s->field_assign.field_name);
    if (fi < 0) return ERR_UNDEFINED_FIELD;
    value_t v;
    EVAL_CHECK(eval_expr(env, s->field_assign.value, &v));
    /* Coerce to field type */
    if (td->field_types[fi] == VAL_INTEGER && v.type != VAL_INTEGER) {
        int iv;
        error_t err = val_to_integer(&v, &iv);
        val_clear(&v);
        if (err != ERR_NONE) return err;
        v = val_integer(iv);
    } else if (td->field_types[fi] == VAL_STRING && v.type != VAL_STRING) {
        char buf[256];
        error_t err = val_to_string(&v, buf, sizeof(buf));
        val_clear(&v);
        if (err != ERR_NONE) return err;
        v = val_string_cstr(buf);
    }
    val_assign(&inst->rval->fields[fi], &v);
    val_clear(&v);
    return ERR_NONE;
}

/* --- MID$ l-value assignment --- */

static error_t exec_mid_assign(env_t *env, stmt_t *s) {
    if (env_is_const(env, s->mid_assign.var_name))
        return ERR_CONST_REASSIGN;

    value_t *sv = env_get(env, s->mid_assign.var_name);
    if (!sv) return ERR_UNDEFINED_VAR;
    if (sv->type != VAL_STRING) return ERR_TYPE_MISMATCH;

    /* Evaluate start position (1-based) */
    value_t start_v;
    EVAL_CHECK(eval_expr(env, s->mid_assign.start, &start_v));
    int start;
    error_t err = val_to_integer(&start_v, &start);
    val_clear(&start_v);
    if (err != ERR_NONE) return err;

    int slen = sv->sval->len;
    start--; /* convert to 0-based */
    if (start < 0 || start >= slen) return ERR_NONE; /* out of range: no change */

    /* Evaluate optional length */
    int maxlen = slen - start;
    if (s->mid_assign.length) {
        value_t len_v;
        EVAL_CHECK(eval_expr(env, s->mid_assign.length, &len_v));
        int length;
        err = val_to_integer(&len_v, &length);
        val_clear(&len_v);
        if (err != ERR_NONE) return err;
        if (length < 0) return ERR_ILLEGAL_FUNCTION_CALL;
        if (length < maxlen) maxlen = length;
    }

    /* Evaluate replacement string */
    value_t rep_v;
    EVAL_CHECK(eval_expr(env, s->mid_assign.value, &rep_v));
    if (rep_v.type != VAL_STRING) { val_clear(&rep_v); return ERR_TYPE_MISMATCH; }

    int rep_len = rep_v.sval->len;
    int replace_count = (rep_len < maxlen) ? rep_len : maxlen;

    /* Build result: same length as original */
    char *buf = malloc(slen + 1);
    if (!buf) { val_clear(&rep_v); return ERR_OUT_OF_MEMORY; }
    memcpy(buf, sv->sval->data, slen);
    memcpy(buf + start, rep_v.sval->data, replace_count);
    buf[slen] = '\0';
    val_clear(&rep_v);

    value_t result = val_string(buf, slen);
    free(buf);
    env_set(env, s->mid_assign.var_name, &result);
    val_clear(&result);
    return ERR_NONE;
}

/* --- Statement dispatch --- */

error_t eval_stmt(env_t *env, stmt_t *s) {
    if (trace_enabled && s->type != STMT_REM && s->type != STMT_LABEL
        && s->type != STMT_DATA && s->type != STMT_TRACE)
        col_printf("[%d] ", s->line);
    switch (s->type) {
        case STMT_PRINT:        return exec_print(env, s);
        case STMT_LPRINT:       return exec_lprint(env, s);
        case STMT_INPUT:        return exec_input(env, s);
        case STMT_CLS:          return exec_cls(s);
        case STMT_LOCATE:       return exec_locate(env, s);
        case STMT_COLOR:        return exec_color(env, s);
        case STMT_ASSIGN:       return exec_assign(env, s);
        case STMT_IF:           return exec_if(env, s);
        case STMT_FOR:          return exec_for(env, s);
        case STMT_WHILE:        return exec_while(env, s);
        case STMT_DO_LOOP:      return exec_do_loop(env, s);
        case STMT_SELECT:       return exec_select(env, s);
        case STMT_CALL:         return exec_call_stmt(env, s);
        case STMT_CONST:        return exec_const(env, s);
        case STMT_DIM:          return exec_dim(env, s);
        case STMT_ARRAY_ASSIGN: return exec_array_assign(env, s);
        case STMT_ERASE:        return exec_erase(env, s);
        case STMT_READ:         return exec_read(env, s);
        case STMT_RESTORE:      return exec_restore(s);
        case STMT_END:          return ERR_EXIT;
        case STMT_REM:          return ERR_NONE;
        case STMT_LABEL:        return ERR_NONE;
        case STMT_DECLARE:      return ERR_NONE;
        case STMT_SHARED:       return ERR_NONE;
        case STMT_STATIC:       return ERR_NONE;
        case STMT_DATA:         return ERR_NONE;

        case STMT_OPTION_BASE:
            option_base = s->option_base.base;
            return ERR_NONE;

        case STMT_SUB_DEF:
        case STMT_FUNC_DEF:
            return ERR_NONE;

        case STMT_EXIT:
            switch (s->exit_stmt.what) {
                case EXIT_FOR:      return ERR_EXIT_FOR;
                case EXIT_WHILE:    return ERR_EXIT_WHILE;
                case EXIT_DO:       return ERR_EXIT_DO;
                case EXIT_SUB:      return ERR_EXIT_SUB;
                case EXIT_FUNCTION: return ERR_EXIT_FUNCTION;
            }
            return ERR_INTERNAL;

        case STMT_GOTO: {
            int idx = find_label(s->goto_stmt.label);
            if (idx < 0) return ERR_UNDEFINED_LABEL;
            goto_target = idx;
            return ERR_GOTO;
        }

        case STMT_GOSUB: {
            int idx = find_label(s->goto_stmt.label);
            if (idx < 0) return ERR_UNDEFINED_LABEL;
            goto_target = idx;
            return ERR_GOSUB;
        }

        case STMT_RETURN:
            strncpy(return_label, s->goto_stmt.label, 63);
            return_label[63] = '\0';
            return ERR_RETURN;

        case STMT_SWAP: {
            if (lvalue_is_const(env, s->swap_stmt.lhs))
                return ERR_CONST_REASSIGN;
            if (lvalue_is_const(env, s->swap_stmt.rhs))
                return ERR_CONST_REASSIGN;
            value_t *v1, *v2;
            error_t e1 = resolve_lvalue(env, s->swap_stmt.lhs, &v1);
            if (e1 != ERR_NONE) return e1;
            error_t e2 = resolve_lvalue(env, s->swap_stmt.rhs, &v2);
            if (e2 != ERR_NONE) return e2;
            value_t tmp = *v1;
            *v1 = *v2;
            *v2 = tmp;
            return ERR_NONE;
        }

        case STMT_RANDOMIZE: {
            int seed = 42;
            if (s->randomize.seed) {
                value_t v;
                EVAL_CHECK(eval_expr(env, s->randomize.seed, &v));
                error_t cerr = val_to_integer(&v, &seed);
                val_clear(&v);
                if (cerr != ERR_NONE) return cerr;
            }
            builtin_randomize(seed);
            return ERR_NONE;
        }

        case STMT_SLEEP: {
            unsigned int secs = 0;
            if (s->sleep_stmt.duration) {
                value_t v;
                EVAL_CHECK(eval_expr(env, s->sleep_stmt.duration, &v));
                int isecs;
                error_t cerr = val_to_integer(&v, &isecs);
                val_clear(&v);
                if (cerr != ERR_NONE) return cerr;
                if (isecs < 0) return ERR_ILLEGAL_FUNCTION_CALL;
                secs = (unsigned int)isecs;
            }
            if (secs > 0) sleep(secs);
            return ERR_NONE;
        }

        case STMT_OPEN:         return exec_open(env, s);
        case STMT_CLOSE:        return exec_close(env, s);
        case STMT_PRINT_FILE:   return exec_print_file(env, s);
        case STMT_WRITE_FILE:   return exec_write_file(env, s);
        case STMT_INPUT_FILE:   return exec_input_file(env, s);
        case STMT_LINE_INPUT:   return exec_line_input(env, s);
        case STMT_LINE_INPUT_CONSOLE: return exec_line_input_console(env, s);
        case STMT_WRITE_CONSOLE: return exec_write_console(env, s);
        case STMT_DEF_FN: {
            /* Register DEF FN in proc table */
            if (proc_count >= MAX_PROCS) return ERR_OUT_OF_MEMORY;
            strncpy(proc_table[proc_count].name, s->def_fn.name, 63);
            proc_table[proc_count].name[63] = '\0';
            proc_table[proc_count].def = s;
            proc_table[proc_count].static_env = NULL;
            proc_count++;
            return ERR_NONE;
        }

        case STMT_SHELL: {
            value_t cmd;
            EVAL_CHECK(eval_expr(env, s->shell_stmt.command, &cmd));
            if (cmd.type != VAL_STRING) { val_clear(&cmd); return ERR_TYPE_MISMATCH; }
            col_puts("SHELL not supported: ");
            col_puts(cmd.sval ? cmd.sval->data : "");
            col_puts("\n");
            val_clear(&cmd);
            return ERR_NONE;
        }

        case STMT_CHDIR: {
            value_t path;
            EVAL_CHECK(eval_expr(env, s->shell_stmt.command, &path));
            if (path.type != VAL_STRING) { val_clear(&path); return ERR_TYPE_MISMATCH; }
            int r = chdir(path.sval ? path.sval->data : "");
            val_clear(&path);
            if (r != 0) return ERR_FILE_NOT_FOUND;
            return ERR_NONE;
        }

        case STMT_MKDIR: {
            value_t path;
            EVAL_CHECK(eval_expr(env, s->shell_stmt.command, &path));
            if (path.type != VAL_STRING) { val_clear(&path); return ERR_TYPE_MISMATCH; }
            int r = mkdir(path.sval ? path.sval->data : "", 0777);
            val_clear(&path);
            if (r != 0) return ERR_FILE_NOT_FOUND;
            return ERR_NONE;
        }

        case STMT_RMDIR: {
            value_t path;
            EVAL_CHECK(eval_expr(env, s->shell_stmt.command, &path));
            if (path.type != VAL_STRING) { val_clear(&path); return ERR_TYPE_MISMATCH; }
            int r = rmdir(path.sval ? path.sval->data : "");
            val_clear(&path);
            if (r != 0) return ERR_FILE_NOT_FOUND;
            return ERR_NONE;
        }

        case STMT_POKE: {
            extern int def_seg;
            value_t av, vv;
            EVAL_CHECK(eval_expr(env, s->poke_stmt.addr, &av));
            error_t err = eval_expr(env, s->poke_stmt.value, &vv);
            if (err != ERR_NONE) { val_clear(&av); return err; }
            int offset, val;
            err = val_to_integer(&av, &offset);
            if (err == ERR_NONE) err = val_to_integer(&vv, &val);
            val_clear(&av); val_clear(&vv);
            if (err != ERR_NONE) return err;
            int addr = def_seg * 16 + offset;
            if (addr < 0 || addr >= PEEK_POKE_SIZE) return ERR_ILLEGAL_FUNCTION_CALL;
            peek_poke_mem[addr] = (unsigned char)(val & 0xFF);
            return ERR_NONE;
        }

        case STMT_DEF_SEG: {
            extern int def_seg;
            if (s->shell_stmt.command) {
                value_t v;
                EVAL_CHECK(eval_expr(env, s->shell_stmt.command, &v));
                int seg;
                error_t err = val_to_integer(&v, &seg);
                val_clear(&v);
                if (err != ERR_NONE) return err;
                def_seg = seg;
            } else {
                def_seg = 0;
            }
            return ERR_NONE;
        }

        case STMT_SCREEN: {
            /* No-op: accept SCREEN mode but ignore it (text-only) */
            value_t mode;
            EVAL_CHECK(eval_expr(env, s->shell_stmt.command, &mode));
            val_clear(&mode);
            return ERR_NONE;
        }

        case STMT_LSET:
        case STMT_RSET: {
            /* LSET/RSET var$ = expr$: pad/truncate to var's current length */
            value_t *cur = env_get(env, s->lrset.varname);
            if (!cur || cur->type != VAL_STRING) return ERR_TYPE_MISMATCH;
            int curlen = cur->sval ? cur->sval->len : 0;
            if (curlen == 0) return ERR_NONE; /* zero-length: nothing to do */
            value_t nv;
            EVAL_CHECK(eval_expr(env, s->lrset.value, &nv));
            if (nv.type != VAL_STRING) { val_clear(&nv); return ERR_TYPE_MISMATCH; }
            const char *src = nv.sval ? nv.sval->data : "";
            int srclen = nv.sval ? nv.sval->len : 0;
            char *buf = malloc(curlen + 1);
            if (!buf) { val_clear(&nv); return ERR_OUT_OF_MEMORY; }
            memset(buf, ' ', curlen);
            buf[curlen] = '\0';
            int copylen = srclen < curlen ? srclen : curlen;
            if (s->type == STMT_LSET) {
                memcpy(buf, src, copylen);
            } else {
                memcpy(buf + curlen - copylen, src, copylen);
            }
            val_clear(&nv);
            value_t result = val_string(buf, curlen);
            free(buf);
            env_set(env, s->lrset.varname, &result);
            val_clear(&result);
            return ERR_NONE;
        }

        case STMT_WIDTH: {
            extern int console_width;
            value_t v;
            EVAL_CHECK(eval_expr(env, s->width_stmt.columns, &v));
            int w;
            error_t err = val_to_integer(&v, &w);
            val_clear(&v);
            if (err != ERR_NONE) return err;
            if (w >= 20 && w <= 255) console_width = w;
            return ERR_NONE;
        }

        case STMT_NOOP:
            return ERR_NONE;

        case STMT_CLEAR:
            env_clear(env);
            array_clear_all();
            return ERR_NONE;

        case STMT_KILL:         return exec_kill(env, s);
        case STMT_NAME:         return exec_name(env, s);
        case STMT_GET:          return exec_get(env, s);
        case STMT_PUT:          return exec_put(env, s);
        case STMT_SEEK:         return exec_seek(env, s);

        case STMT_TYPE_DEF:     return exec_type_def(s);
        case STMT_DIM_AS_TYPE:  return exec_dim_as_type(env, s);
        case STMT_FIELD_ASSIGN: return exec_field_assign(env, s);
        case STMT_MID_ASSIGN:   return exec_mid_assign(env, s);

        case STMT_ON_ERROR: {
            if (s->on_error.label[0] == '\0') {
                /* ON ERROR GOTO 0: disable error trapping */
                on_error_label_idx = -1;
            } else {
                int idx = find_label(s->on_error.label);
                if (idx < 0) return ERR_UNDEFINED_LABEL;
                on_error_label_idx = idx;
            }
            on_error_active = 0;
            return ERR_NONE;
        }

        case STMT_RESUME:
            resume_is_next = s->resume_stmt.resume_next;
            if (resume_is_next == 2) {
                strncpy(resume_label, s->resume_stmt.label, 63);
                resume_label[63] = '\0';
            }
            return ERR_RESUME_WITHOUT_ERROR; /* handled in eval_program loop */

        case STMT_ERROR_RAISE: {
            value_t v;
            EVAL_CHECK(eval_expr(env, s->error_raise.errnum, &v));
            int code;
            error_t cerr = val_to_integer(&v, &code);
            val_clear(&v);
            if (cerr != ERR_NONE) return cerr;
            on_error_err_code = code;
            on_error_err_line = s->line;
            return ERR_ON_ERROR_GOTO;
        }

        case STMT_DEFTYPE: {
            char from = s->deftype.from;
            char to = s->deftype.to;
            if (from >= 'A' && from <= 'Z' && to >= 'A' && to <= 'Z') {
                for (char c = from; c <= to; c++)
                    deftype_map[c - 'A'] = s->deftype.def_type;
            }
            return ERR_NONE;
        }

        case STMT_ON_GOTO: {
            value_t v;
            EVAL_CHECK(eval_expr(env, s->on_branch.index, &v));
            int idx;
            error_t cerr = val_to_integer(&v, &idx);
            val_clear(&v);
            if (cerr != ERR_NONE) return cerr;
            if (idx < 1 || idx > s->on_branch.nlabels)
                return ERR_NONE; /* out of range: just continue */
            int target = find_label(s->on_branch.labels[idx - 1]);
            if (target < 0) return ERR_UNDEFINED_LABEL;
            goto_target = target;
            return ERR_GOTO;
        }

        case STMT_ON_GOSUB: {
            value_t v;
            EVAL_CHECK(eval_expr(env, s->on_branch.index, &v));
            int idx;
            error_t cerr = val_to_integer(&v, &idx);
            val_clear(&v);
            if (cerr != ERR_NONE) return cerr;
            if (idx < 1 || idx > s->on_branch.nlabels)
                return ERR_NONE; /* out of range: just continue */
            int target = find_label(s->on_branch.labels[idx - 1]);
            if (target < 0) return ERR_UNDEFINED_LABEL;
            goto_target = target;
            return ERR_GOSUB;
        }

        case STMT_BEEP:
            return ERR_NONE;

        case STMT_OPTION_EXPLICIT:
            option_explicit = 1;
            return ERR_NONE;

        case STMT_DIM_SCALAR: {
            /* Declare scalar variable with default value */
            value_t def;
            if (s->dim_scalar.scalar_type == VAL_INTEGER)
                def = val_integer(0);
            else if (s->dim_scalar.scalar_type == VAL_DOUBLE)
                def = val_double(0.0);
            else
                def = val_string_cstr("");
            env_set(env, s->dim_scalar.name, &def);
            val_clear(&def);
            return ERR_NONE;
        }

        case STMT_TRACE:
            trace_enabled = s->trace.enabled;
            return ERR_NONE;

        case STMT_SPLIT: {
            /* SPLIT str$, delim$, array$() */
            value_t sv, dv;
            EVAL_CHECK(eval_expr(env, s->split.str_expr, &sv));
            error_t err2 = eval_expr(env, s->split.delim_expr, &dv);
            if (err2 != ERR_NONE) { val_clear(&sv); return err2; }
            const char *str = (sv.type == VAL_STRING && sv.sval) ? sv.sval->data : "";
            const char *delim = (dv.type == VAL_STRING && dv.sval) ? dv.sval->data : "";
            int dlen = (int)strlen(delim);

            /* Count pieces */
            int count = 1;
            if (dlen > 0) {
                const char *p = str;
                while ((p = strstr(p, delim)) != NULL) { count++; p += dlen; }
            }

            /* REDIM the target array */
            int sizes[1] = { count };
            array_erase(s->split.array_name);
            error_t aerr = array_dim(s->split.array_name, VAL_STRING,
                                      sizes, 1, option_base);
            if (aerr != ERR_NONE) { val_clear(&sv); val_clear(&dv); return aerr; }

            /* Split and store */
            const char *p = str;
            for (int i = 0; i < count; i++) {
                const char *next = (dlen > 0) ? strstr(p, delim) : NULL;
                int plen = next ? (int)(next - p) : (int)strlen(p);
                if (plen > MAX_STRING_LEN) plen = MAX_STRING_LEN;
                value_t piece = val_string(p, plen);
                int idx[1] = { i + option_base };
                array_set(s->split.array_name, idx, 1, &piece);
                val_clear(&piece);
                p = next ? next + dlen : p + plen;
            }
            val_clear(&sv);
            val_clear(&dv);
            return ERR_NONE;
        }
    }

    return ERR_INTERNAL;
}

error_t eval_stmts(env_t *env, stmt_t *stmts) {
    if (++eval_depth > MAX_EVAL_DEPTH) {
        eval_depth--;
        return ERR_STACK_OVERFLOW;
    }
    stmt_t *s = stmts;
    while (s) {
        error_t err = eval_stmt(env, s);
        if (err != ERR_NONE) { eval_depth--; return err; }
        s = s->next;
    }
    eval_depth--;
    return ERR_NONE;
}

/* --- Program execution with GOTO/GOSUB/SUB/FUNCTION support --- */

static void data_pool_clear(void) {
    for (int i = 0; i < data_pool_count; i++)
        val_clear(&data_pool[i]);
    data_pool_count = 0;
    data_read_ptr = 0;
}

/* Recursively scan a statement list for labels, DATA, and SUB/FUNCTION defs.
 * top_idx is the top-level stmts[] index (used for label GOTO targets). */
static error_t scan_stmt_list(stmt_t *s, int top_idx);

static error_t scan_one_stmt(stmt_t *s, int top_idx) {
    if (s->type == STMT_LABEL) {
        if (label_count >= MAX_LABELS) return ERR_OUT_OF_MEMORY;
        strncpy(label_table[label_count].name, s->label.name, 63);
        label_table[label_count].name[63] = '\0';
        label_table[label_count].index = top_idx;
        label_table[label_count].data_offset = data_pool_count;
        label_count++;
    }
    if (s->type == STMT_DATA) {
        for (int j = 0; j < s->data_stmt.nvalues; j++) {
            if (data_pool_count >= MAX_DATA_VALUES)
                return ERR_OUT_OF_MEMORY;
            data_pool[data_pool_count++] =
                val_copy(&s->data_stmt.values[j]);
        }
    }
    if (s->type == STMT_SUB_DEF || s->type == STMT_FUNC_DEF) {
        if (proc_count >= MAX_PROCS) return ERR_OUT_OF_MEMORY;
        strncpy(proc_table[proc_count].name, s->proc_def.name, 63);
        proc_table[proc_count].name[63] = '\0';
        proc_table[proc_count].def = s;
        proc_table[proc_count].static_env = NULL;
        proc_count++;
    }
    /* Recurse into block bodies */
    error_t err;
    switch (s->type) {
        case STMT_IF:
            err = scan_stmt_list(s->if_stmt.then_body, top_idx);
            if (err != ERR_NONE) return err;
            if (s->if_stmt.else_body) {
                err = scan_stmt_list(s->if_stmt.else_body, top_idx);
                if (err != ERR_NONE) return err;
            }
            break;
        case STMT_FOR:
            err = scan_stmt_list(s->for_stmt.body, top_idx);
            if (err != ERR_NONE) return err;
            break;
        case STMT_WHILE:
            err = scan_stmt_list(s->while_stmt.body, top_idx);
            if (err != ERR_NONE) return err;
            break;
        case STMT_DO_LOOP:
            err = scan_stmt_list(s->do_loop.body, top_idx);
            if (err != ERR_NONE) return err;
            break;
        case STMT_SELECT:
            for (case_clause_t *c = s->select_stmt.clauses; c; c = c->next) {
                err = scan_stmt_list(c->body, top_idx);
                if (err != ERR_NONE) return err;
            }
            break;
        default:
            break;
    }
    return ERR_NONE;
}

static error_t scan_stmt_list(stmt_t *s, int top_idx) {
    for (; s; s = s->next) {
        error_t err = scan_one_stmt(s, top_idx);
        if (err != ERR_NONE) return err;
    }
    return ERR_NONE;
}

error_t eval_program(env_t *env, stmt_t *program) {
    int count = 0;
    for (stmt_t *s = program; s; s = s->next)
        count++;

    if (count == 0) return ERR_NONE;

    stmt_t **stmts = malloc(count * sizeof(stmt_t *));
    if (!stmts) return ERR_OUT_OF_MEMORY;

    int i = 0;
    for (stmt_t *s = program; s; s = s->next)
        stmts[i++] = s;

    /* Initialize program state */
    program_global_env = env;
    option_base = 0;
    array_clear_all();
    data_pool_clear();
    fileio_init();
    sb_term_init();
    deftype_init();
    type_def_count = 0;
    print_col = 0;
    print_row = 1;
    eval_depth = 0;
    on_error_label_idx = -1;
    on_error_active = 0;
    on_error_err_code = 0;
    on_error_err_line = 0;
    resume_is_next = 0;
    resume_label[0] = '\0';
    return_label[0] = '\0';
    trace_enabled = 0;
    option_explicit = 0;

    error_t result = ERR_NONE;

    /* First pass: collect labels, proc definitions, and DATA values */
    proc_count = 0;
    label_count = 0;

    for (i = 0; i < count; i++) {
        error_t serr = scan_one_stmt(stmts[i], i);
        if (serr != ERR_NONE) {
            result = serr;
            goto cleanup;
        }
    }

    /* Execute with program counter */
    int pc = 0;
    gosub_top = 0;
    while (pc < count) {
        if (stmts[pc]->type == STMT_SUB_DEF ||
            stmts[pc]->type == STMT_FUNC_DEF) {
            pc++;
            continue;
        }

        error_t err = eval_stmt(env, stmts[pc]);

        if (err == ERR_NONE) {
            pc++;
            continue;
        }

        if (err == ERR_EXIT)
            break;

        if (err == ERR_GOTO) {
            pc = goto_target;
            continue;
        }

        if (err == ERR_GOSUB) {
            if (gosub_top >= MAX_GOSUB_DEPTH) {
                result = ERR_STACK_OVERFLOW;
                break;
            }
            gosub_stack[gosub_top++] = pc + 1;
            pc = goto_target;
            continue;
        }

        if (err == ERR_RETURN) {
            if (gosub_top <= 0) {
                result = ERR_RETURN_WITHOUT_GOSUB;
                break;
            }
            gosub_top--;  /* pop the stack */
            if (return_label[0]) {
                /* RETURN label — jump to specified label instead of caller */
                int idx = find_label(return_label);
                if (idx < 0) { result = ERR_UNDEFINED_LABEL; break; }
                pc = idx;
                return_label[0] = '\0';
            } else {
                pc = gosub_stack[gosub_top];
            }
            continue;
        }

        /* RESUME / RESUME NEXT / RESUME label (from error handler) */
        if (err == ERR_RESUME_WITHOUT_ERROR && on_error_active) {
            on_error_active = 0;
            if (resume_is_next == 1)
                pc = on_error_resume_next_pc;
            else if (resume_is_next == 2) {
                int idx = find_label(resume_label);
                if (idx < 0) { err = ERR_UNDEFINED_LABEL; break; }
                pc = idx;
            } else
                pc = on_error_resume_pc;
            continue;
        }

        /* ON ERROR GOTO triggered by ERROR n statement */
        if (err == ERR_ON_ERROR_GOTO && on_error_label_idx >= 0 &&
            !on_error_active) {
            on_error_active = 1;
            on_error_resume_pc = pc;
            on_error_resume_next_pc = pc + 1;
            pc = on_error_label_idx;
            continue;
        }

        /* Error trapping: if ON ERROR GOTO is active and this is a
           trappable error, jump to the handler */
        if (on_error_label_idx >= 0 && !on_error_active &&
            is_trappable_error(err)) {
            on_error_err_code = (int)err;
            on_error_err_line = stmts[pc]->line;
            on_error_active = 1;
            on_error_resume_pc = pc;
            on_error_resume_next_pc = pc + 1;
            pc = on_error_label_idx;
            continue;
        }

        result = err;
        break;
    }

cleanup:
    /* Clean up */
    fileio_close_all();
    data_pool_clear();
    array_clear_all();
    for (i = 0; i < proc_count; i++) {
        if (proc_table[i].static_env) {
            env_destroy(proc_table[i].static_env);
            proc_table[i].static_env = NULL;
        }
    }
    free(stmts);
    sb_term_shutdown();
    return result;
}
