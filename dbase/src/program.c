#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "program.h"
#include "command.h"
#include "func.h"
#include "util.h"
#include "lex.h"
#include "area.h"
#include "set.h"
#include "ast.h"
#include "screen.h"

static prog_state_t state;

/* SET PROCEDURE TO file — resident procedure library */
static program_t *procedure_file;

/* ---- Error state ---- */
static struct {
    int code;               /* last error number (0 = none) */
    char message[256];      /* last error message text */
    int lineno;             /* line where error occurred (1-based, 0=interactive) */
    char program[64];       /* program filename, ""=interactive */
    char on_error_proc[64]; /* ON ERROR handler procedure name, ""=none */
    int in_handler;         /* 1 = currently inside error handler */
    int retry;              /* set by RETRY command inside handler */
} error_state;

/* User-defined FUNCTION support */
static int udf_active;          /* >0 when executing a UDF call */
static value_t udf_return_val;  /* return value from FUNCTION */
static int udf_depth;           /* call_depth at UDF entry */
static int udf_call_nesting;    /* host C recursion depth through UDF callbacks */
static int prog_udf_call(const char *name, value_t *args, int nargs, value_t *result);
#define MAX_UDF_CALL_NESTING 8

/* IF/ELSE/ENDIF nesting for skip mode */
#define MAX_IF_DEPTH 32
static int if_skip;             /* >0 = skipping lines */
static int if_depth;            /* current IF nesting depth */
static int if_done[MAX_IF_DEPTH]; /* branch already taken at this depth */

/* DO CASE state */
static int case_active;         /* inside DO CASE block */
static int case_done;           /* a CASE branch has been taken */
static int case_skip;           /* skipping until matching CASE/OTHERWISE/ENDCASE */

/* ---- Suspended state for SUSPEND/RESUME ---- */
static struct {
    int if_skip, if_depth;
    int if_done[MAX_IF_DEPTH];
    int case_active, case_done, case_skip;
    int valid;  /* 1 = suspended state exists */
} suspended;

/* ---- BEGIN SEQUENCE / RECOVER / END SEQUENCE ---- */
typedef struct {
    int pc;
    int call_depth;
    int loop_depth;
    program_t *prog;
} sequence_frame_t;

static sequence_frame_t seq_stack[MAX_SEQUENCE_DEPTH];
static int seq_depth;

/* ---- Synchronous procedure call support ---- */
/* When prog_call_sync is active, pop_frame stops execution when
   call_depth returns to the target level. */
static int sync_call_active;
static int sync_call_depth;

typedef struct {
    program_t *saved_prog;
    int saved_pc;
    int saved_call_depth;
    int saved_loop_depth;
    int saved_running;
    int saved_if_skip;
    int saved_if_depth;
    int saved_case_active;
    int saved_case_done;
    int saved_case_skip;
    int saved_udf_active;
    int saved_udf_depth;
    value_t saved_udf_return;
} udf_saved_state_t;

/* Recursion-resistant scratch/state storage keyed by call depth. */
static udf_saved_state_t udf_saved_stack[MAX_CALL_DEPTH + 2];
static char run_line_buf[MAX_CALL_DEPTH + 2][MAX_LINE_LEN];
static char run_prev_buf[MAX_CALL_DEPTH + 2][MAX_LINE_LEN];
static char run_line2_buf[MAX_CALL_DEPTH + 2][MAX_LINE_LEN];

static void udf_restore_state(const udf_saved_state_t *sv) {
    state.current_prog = sv->saved_prog;
    state.pc = sv->saved_pc;
    state.call_depth = sv->saved_call_depth;
    state.loop_depth = sv->saved_loop_depth;
    state.running = sv->saved_running;
    if_skip = sv->saved_if_skip;
    if_depth = sv->saved_if_depth;
    case_active = sv->saved_case_active;
    case_done = sv->saved_case_done;
    case_skip = sv->saved_case_skip;
    udf_active = sv->saved_udf_active;
    udf_depth = sv->saved_udf_depth;
    udf_return_val = sv->saved_udf_return;
}

void prog_init(void) {
    memset(&state, 0, sizeof(state));
    memset(&error_state, 0, sizeof(error_state));
    memset(&suspended, 0, sizeof(suspended));
    if_skip = 0;
    if_depth = 0;
    case_active = 0;
    case_done = 0;
    case_skip = 0;
    sync_call_active = 0;
    sync_call_depth = 0;
    func_set_udf_callback(prog_udf_call);
}

/* Forward declarations needed by error handling */
static void prog_run(void);
static int scan_sequence_end(program_t *prog, int from, int kind);
static int line_is_kw(const char *line, const char *kw);
static void save_private(call_frame_t *frame, const char *name);
static void restore_privates(call_frame_t *frame);

/* Helper: map expression error string to error code */
int expr_error_code(const char *msg) {
    if (!msg) return ERR_SYNTAX;
    if (strstr(msg, "ivision by zero")) return ERR_DIV_ZERO;
    if (strstr(msg, "ype mismatch")) return ERR_TYPE_MISMATCH;
    return ERR_SYNTAX;
}

/* ---- Error handling ---- */

void prog_error(int code, const char *message) {
    error_state.code = code;
    str_copy(error_state.message, message, sizeof(error_state.message));

    /* Capture line number and program name */
    if (state.running && state.current_prog) {
        error_state.lineno = state.pc + 1;  /* 1-based */
        str_copy(error_state.program, state.current_prog->filename,
                 sizeof(error_state.program));
    } else {
        error_state.lineno = 0;
        error_state.program[0] = '\0';
    }

    /* If ON ERROR handler is set and we're not already in it, invoke it.
       Use prog_call_sync so the handler runs to completion before we
       check RETRY — prog_do alone only pushes a frame and relies on
       the caller's prog_run loop, which doesn't work when prog_error
       is called from deep inside a command handler (e.g. REPLACE). */
    if (error_state.on_error_proc[0] && !error_state.in_handler) {
        error_state.in_handler = 1;
        error_state.retry = 0;
        prog_call_sync(error_state.on_error_proc);
        if (error_state.retry && state.running) {
            /* Decrement PC so the failed line re-executes */
            if (state.pc > 0) state.pc--;
        }
        error_state.in_handler = 0;
        error_state.retry = 0;
        return;
    }

    /* BEGIN SEQUENCE recovery — scan forward for RECOVER/END SEQUENCE.
       Note: prog_execute_line does state.pc++ after cmd_execute returns,
       so we set pc to the target line; the increment lands on target+1. */
    if (seq_depth > 0 && state.running) {
        seq_depth--;
        sequence_frame_t *f = &seq_stack[seq_depth];

        /* Unwind call/loop depth */
        while (state.call_depth > f->call_depth)
            state.call_depth--;
        state.loop_depth = f->loop_depth;
        state.current_prog = f->prog;
        cmd_get_memvar_store()->current_depth = state.call_depth;

        /* Scan from BEGIN SEQUENCE for RECOVER or END SEQUENCE */
        int target = scan_sequence_end(f->prog, f->pc, 0);
        if (target >= 0) {
            char line[MAX_LINE_LEN];
            char *p;
            str_copy(line, f->prog->lines[target], MAX_LINE_LEN);
            prog_preprocess(line, cmd_get_memvar_store());
            p = skip_ws(line);
            if (line_is_kw(p, "RECOVER"))
                state.pc = target;  /* pc++ will land on first line after RECOVER */
            else
                state.pc = target;  /* pc++ will land on line after END SEQUENCE */
        } else {
            state.pc = f->pc;
        }
        return;
    }

    /* No handler — print error with context */
    if (error_state.lineno > 0 && error_state.program[0]) {
        printf("*** %s in %s line %d\n", message,
               error_state.program, error_state.lineno);
    } else {
        printf("*** %s\n", message);
    }
}

void prog_error_fmt(int code, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    prog_error(code, buf);
}

int prog_get_error_code(void) {
    return error_state.code;
}

const char *prog_get_error_message(void) {
    return error_state.message;
}

int prog_get_lineno(void) {
    return error_state.lineno;
}

const char *prog_get_program_name(void) {
    return error_state.program;
}

void prog_on_error(const char *procname) {
    if (procname && procname[0]) {
        str_copy(error_state.on_error_proc, procname,
                 sizeof(error_state.on_error_proc));
    } else {
        error_state.on_error_proc[0] = '\0';
    }
}

void prog_retry(void) {
    if (!error_state.in_handler) {
        printf("*** RETRY not in error handler\n");
        return;
    }
    error_state.retry = 1;
}

/* ---- SUSPEND / RESUME ---- */

void prog_suspend(void) {
    if (!state.running) {
        printf("*** Not in program\n");
        return;
    }
    /* Save skip state */
    suspended.if_skip = if_skip;
    suspended.if_depth = if_depth;
    memcpy(suspended.if_done, if_done, sizeof(if_done));
    suspended.case_active = case_active;
    suspended.case_done = case_done;
    suspended.case_skip = case_skip;
    suspended.valid = 1;

    /* Pause execution — prog_run() exits when running == 0 */
    state.running = 0;
    printf("Suspended.\n");
}

void prog_resume(void) {
    if (!suspended.valid) {
        printf("*** Nothing suspended\n");
        return;
    }
    /* Restore skip state */
    if_skip = suspended.if_skip;
    if_depth = suspended.if_depth;
    memcpy(if_done, suspended.if_done, sizeof(if_done));
    case_active = suspended.case_active;
    case_done = suspended.case_done;
    case_skip = suspended.case_skip;
    suspended.valid = 0;

    /* Continue execution */
    state.running = 1;
    prog_run();
}

int prog_is_running(void) {
    return state.running;
}

memvar_store_t *prog_get_memvar_store(void) {
    return cmd_get_memvar_store();
}

/* ---- Preprocessing: comments, && ---- */
void prog_preprocess(char *line, memvar_store_t *store) {
    char *p;
    (void)store;

    /* Strip && inline comments */
    p = line;
    while (*p) {
        if (*p == '&' && *(p + 1) == '&') {
            *p = '\0';
            trim_right(line);
            break;
        }
        /* Skip string literals */
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (*p && *p != q) p++;
            if (*p) p++;
            continue;
        }
        p++;
    }
}

/* ---- Load program file ---- */
static program_t *prog_load(const char *filename) {
    FILE *fp;
    char path[128];
    char line[MAX_LINE_LEN];
    program_t *prog;
    int capacity = 128;

    str_copy(path, filename, sizeof(path));
    /* Add .PRG extension if not present */
    if (strlen(path) < 4 || str_icmp(path + strlen(path) - 4, ".PRG") != 0) {
        if (strlen(path) + 4 < sizeof(path))
            strcat(path, ".PRG");
    }

    /* Try original case first, then uppercase, then all lowercase */
    fp = fopen(path, "r");
    if (!fp) {
        str_upper(path);
        fp = fopen(path, "r");
    }
    if (!fp) {
        /* Try all lowercase (common on Linux) */
        int pi;
        for (pi = 0; path[pi]; pi++)
            if (path[pi] >= 'A' && path[pi] <= 'Z') path[pi] += 32;
        fp = fopen(path, "r");
    }
    if (!fp) {
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "File not found: %s", path);
            prog_error(ERR_FILE_NOT_FOUND, buf);
        }
        return NULL;
    }

    prog = (program_t *)malloc(sizeof(program_t));
    if (!prog) {
        fclose(fp);
        return NULL;
    }
    memset(prog, 0, sizeof(program_t));
    str_copy(prog->filename, path, sizeof(prog->filename));
    prog->lines = (char **)malloc(capacity * sizeof(char *));
    if (!prog->lines) {
        free(prog);
        fclose(fp);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp)) {
        trim_right(line);

        /* Line continuation: ; at end */
        while (strlen(line) > 0 && line[strlen(line) - 1] == ';') {
            char next[MAX_LINE_LEN];
            line[strlen(line) - 1] = '\0'; /* remove ; */
            if (!fgets(next, sizeof(next), fp)) break;
            trim_right(next);
            /* Skip leading whitespace of continuation */
            {
                char *np = skip_ws(next);
                if (strlen(line) + strlen(np) < MAX_LINE_LEN - 1)
                    strcat(line, np);
            }
        }

        if (prog->nlines >= capacity) {
            capacity *= 2;
            if (capacity > MAX_PROGRAM_LINES) capacity = MAX_PROGRAM_LINES;
            prog->lines = (char **)realloc(prog->lines, capacity * sizeof(char *));
        }
        if (prog->nlines >= MAX_PROGRAM_LINES) {
            printf("Program too long (max %d lines).\n", MAX_PROGRAM_LINES);
            break;
        }

        prog->lines[prog->nlines] = strdup(line);
        prog->nlines++;
    }

    fclose(fp);
    return prog;
}

static void prog_free(program_t *prog) {
    int i;
    if (!prog) return;
    if (prog->lines) {
        for (i = 0; i < prog->nlines; i++)
            free(prog->lines[i]);
        free(prog->lines);
    }
    free(prog);
}

const char *prog_get_procedure_name(void) {
    if (procedure_file && procedure_file->filename[0])
        return procedure_file->filename;
    return NULL;
}

void prog_set_procedure(const char *filename) {
    if (procedure_file) {
        prog_free(procedure_file);
        procedure_file = NULL;
    }
    if (filename && filename[0]) {
        procedure_file = prog_load(filename);
        if (procedure_file && cmd_get_talk())
            printf("Procedure file: %s\n", procedure_file->filename);
    } else {
        if (cmd_get_talk())
            printf("Procedure file released.\n");
    }
}

/* Forward declarations for UDF support */
static int push_frame(program_t *new_prog, int start_line);
static void pop_frame(void);
static void prog_run(void);
static int find_procedure(program_t *prog, const char *name);

/* ---- User-defined function callback (called from expr evaluator) ---- */
static int prog_udf_call(const char *name, value_t *args, int nargs, value_t *result) {
    program_t *target_prog = NULL;
    int func_line = -1;
    int slot = state.call_depth;
    udf_saved_state_t *sv;

    if (slot < 0) slot = 0;
    if (slot >= MAX_CALL_DEPTH + 2) slot = MAX_CALL_DEPTH + 1;
    sv = &udf_saved_stack[slot];

    /* Save lightweight state (avoid copying 700KB call stack) */
    sv->saved_prog = state.current_prog;
    sv->saved_pc = state.pc;
    sv->saved_call_depth = state.call_depth;
    sv->saved_loop_depth = state.loop_depth;
    sv->saved_running = state.running;
    sv->saved_if_skip = if_skip;
    sv->saved_if_depth = if_depth;
    sv->saved_case_active = case_active;
    sv->saved_case_done = case_done;
    sv->saved_case_skip = case_skip;
    sv->saved_udf_active = udf_active;
    sv->saved_udf_depth = udf_depth;
    sv->saved_udf_return = udf_return_val;

    /* Search current program, then procedure file */
    if (state.running && state.current_prog) {
        func_line = find_procedure(state.current_prog, name);
        if (func_line >= 0)
            target_prog = state.current_prog;
    }
    if (func_line < 0 && procedure_file) {
        func_line = find_procedure(procedure_file, name);
        if (func_line >= 0)
            target_prog = procedure_file;
    }
    if (func_line < 0)
        return 1;  /* not a UDF, let caller report unknown function */

    if (udf_call_nesting >= MAX_UDF_CALL_NESTING) {
        prog_error(ERR_STACK_OVERFLOW, "UDF recursion overflow");
        if (result) *result = val_nil();
        return -1;
    }

    /* Set up UDF execution */
    udf_return_val = val_nil();
    udf_active = 1;
    if_skip = 0;
    if_depth = 0;
    case_active = 0;
    case_done = 0;
    case_skip = 0;

    /* Push frame for the function (uses existing call stack in-place) */
    if (push_frame(target_prog, func_line + 1) < 0)
        goto restore_error;
    udf_depth = state.call_depth;

    /* Store args for PARAMETERS */
    {
        call_frame_t *frame = &state.call_stack[state.call_depth - 1];
        int i;
        frame->with_argc = nargs;
        for (i = 0; i < nargs && i < MAX_FUNC_ARGS; i++)
            frame->with_args[i] = args[i];
    }

    /* Execute the function body — prog_run will stop when RETURN sets running=0 */
    udf_call_nesting++;
    prog_run();
    udf_call_nesting--;

    /* Capture return value */
    *result = udf_return_val;

    /* Restore lightweight state — pop_frame already restored call stack properly */
    udf_restore_state(sv);

    return 0;

restore_error:
    /* Restore state on failed UDF setup (e.g. stack overflow) */
    udf_restore_state(sv);
    if (result) *result = val_nil();
    return -1;
}

/* ---- Find PROCEDURE or FUNCTION <name> in program ---- */
static int line_is_kw(const char *line, const char *kw);

static int find_procedure(program_t *prog, const char *name) {
    int i;
    for (i = 0; i < prog->nlines; i++) {
        char line[MAX_LINE_LEN];
        char *p;
        char *rest;
        char pname[64];
        int j;
        int is_func = 0;

        str_copy(line, prog->lines[i], MAX_LINE_LEN);
        prog_preprocess(line, cmd_get_memvar_store());
        p = skip_ws(line);

        if (line_is_kw(p, "PROCEDURE"))
            is_func = 0;
        else if (line_is_kw(p, "FUNCTION"))
            is_func = 1;
        else
            continue;

        rest = skip_ws(p + (is_func ? 8 : 9));
        j = 0;
        while (is_ident_char(*rest) && j < 63)
            pname[j++] = *rest++;
        pname[j] = '\0';
        if (str_icmp(pname, name) == 0)
            return i;
    }
    return -1;
}

static int line_is_kw(const char *line, const char *kw) {
    char tok[32];
    int i = 0;
    const char *p = skip_ws(line);
    if (!is_ident_start(*p)) return 0;
    while (is_ident_char(*p) && i < (int)sizeof(tok) - 1)
        tok[i++] = *p++;
    tok[i] = '\0';
    if (str_imatch(tok, kw) == 0) return 0;

    /* If keyword is PROCEDURE or FUNCTION, it must be followed by an identifier (name)
       to be considered a definition line. This avoids confusion with assignments like
       PROCEDURE = "something" */
    if (str_imatch(tok, "PROCEDURE") || str_imatch(tok, "FUNCTION")) {
        p = skip_ws(p);
        if (!is_ident_start(*p)) return 0;
    }

    return 1;
}

static int line_is_do_kw(const char *line, const char *kw) {
    char tok[32];
    int i = 0;
    const char *p = skip_ws(line);
    if (!is_ident_start(*p)) return 0;
    while (is_ident_char(*p) && i < (int)sizeof(tok) - 1)
        tok[i++] = *p++;
    tok[i] = '\0';
    if (!str_imatch(tok, "DO")) return 0;
    p = skip_ws(p);
    if (!is_ident_start(*p)) return 0;
    i = 0;
    while (is_ident_char(*p) && i < (int)sizeof(tok) - 1)
        tok[i++] = *p++;
    tok[i] = '\0';
    return str_imatch(tok, kw) != 0;
}

/* ---- Scan forward for matching control structure ---- */
/* Scan from line `from` for matching ELSE/ENDIF at the same nesting level */
static int scan_if(program_t *prog, int from, int want_else) {
    int depth = 1;
    int i;
    for (i = from + 1; i < prog->nlines; i++) {
        char line[MAX_LINE_LEN];
        char *p;
        str_copy(line, prog->lines[i], MAX_LINE_LEN);
        prog_preprocess(line, cmd_get_memvar_store());
        p = skip_ws(line);
        if (line_is_kw(p, "IF")) {
            depth++;
        } else if (line_is_kw(p, "ENDIF")) {
            depth--;
            if (depth == 0) return i;
        } else if (depth == 1 && want_else && line_is_kw(p, "ELSE")) {
            return i;
        }
    }
    return -1;
}

/* Scan from line `from` for matching ENDDO */
static int scan_enddo(program_t *prog, int from) {
    int depth = 1;
    int i;
    for (i = from + 1; i < prog->nlines; i++) {
        char line[MAX_LINE_LEN];
        char *p;
        str_copy(line, prog->lines[i], MAX_LINE_LEN);
        prog_preprocess(line, cmd_get_memvar_store());
        p = skip_ws(line);
        if (line_is_do_kw(p, "WHILE")) {
            depth++;
        } else if (line_is_kw(p, "ENDDO")) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

/* Scan from line `from` for matching NEXT */
static int scan_next(program_t *prog, int from) {
    int depth = 1;
    int i;
    for (i = from + 1; i < prog->nlines; i++) {
        char line[MAX_LINE_LEN];
        char *p;
        str_copy(line, prog->lines[i], MAX_LINE_LEN);
        prog_preprocess(line, cmd_get_memvar_store());
        p = skip_ws(line);
        if (line_is_kw(p, "FOR"))
            depth++;
        else if (line_is_kw(p, "NEXT")) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

/* Scan for ENDCASE from current position */
static int scan_endcase(program_t *prog, int from) {
    int depth = 1;
    int i;
    for (i = from + 1; i < prog->nlines; i++) {
        char line[MAX_LINE_LEN];
        char *p;
        str_copy(line, prog->lines[i], MAX_LINE_LEN);
        prog_preprocess(line, cmd_get_memvar_store());
        p = skip_ws(line);
        if (line_is_do_kw(p, "CASE"))
            depth++;
        else if (line_is_kw(p, "ENDCASE")) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

static int eval_logical(expr_ctx_t *ctx, const char *expr, int *result) {
    value_t res;
    if (expr_eval_str(ctx, expr, &res) != 0) {
        if (ctx->error) prog_error(expr_error_code(ctx->error), ctx->error);
        return -1;
    }
    *result = (res.type == VAL_LOGIC && res.logic);
    return 0;
}

static int eval_ast_logical(expr_ctx_t *ctx, ast_node_t *ast, int *result) {
    value_t res;
    if (ast_eval(ast, ctx, &res) != 0) {
        if (ctx->error) prog_error(expr_error_code(ctx->error), ctx->error);
        return -1;
    }
    *result = (res.type == VAL_LOGIC && res.logic);
    return 0;
}

static int eval_dynamic_logical(expr_ctx_t *ctx, const char *expr, int *result) {
    value_t res;
    if (ast_eval_dynamic(expr, ctx, &res) != 0) {
        if (ctx->error) prog_error(expr_error_code(ctx->error), ctx->error);
        return -1;
    }
    *result = (res.type == VAL_LOGIC && res.logic);
    return 0;
}

static int scan_record_matches(loop_entry_t *loop, dbf_t *db, int *stop_scan) {
    expr_ctx_t *ctx = cmd_get_expr_ctx();
    int ok;

    *stop_scan = 0;

    if (db->record_buf[0] == '*' && ctx->opts->deleted) return 0;

    if (area_get_current()->filter_cond[0]) {
        if (area_get_current()->filter_ast) {
            if (eval_ast_logical(ctx, area_get_current()->filter_ast, &ok) < 0) return -1;
        } else {
            if (eval_dynamic_logical(ctx, area_get_current()->filter_cond, &ok) < 0) return -1;
        }
        if (!ok) return 0;
    }

    if (loop->scan_clause.while_ast) {
        if (eval_ast_logical(ctx, loop->scan_clause.while_ast, &ok) < 0) return -1;
        if (!ok) { *stop_scan = 1; return 0; }
    } else if (loop->scan_clause.while_cond[0]) {
        if (eval_dynamic_logical(ctx, loop->scan_clause.while_cond, &ok) < 0) return -1;
        if (!ok) { *stop_scan = 1; return 0; }
    }

    if (loop->scan_clause.for_ast) {
        if (eval_ast_logical(ctx, loop->scan_clause.for_ast, &ok) < 0) return -1;
        if (!ok) return 0;
    } else if (loop->scan_clause.for_cond[0]) {
        if (eval_dynamic_logical(ctx, loop->scan_clause.for_cond, &ok) < 0) return -1;
        if (!ok) return 0;
    }

    return 1;
}

/* Scan for ENDSCAN from current position */
static int scan_endscan(program_t *prog, int from) {
    int depth = 1;
    int i;
    for (i = from + 1; i < prog->nlines; i++) {
        char line[MAX_LINE_LEN];
        char *p;
        str_copy(line, prog->lines[i], MAX_LINE_LEN);
        prog_preprocess(line, cmd_get_memvar_store());
        p = skip_ws(line);
        if (line_is_kw(p, "SCAN"))
            depth++;
        else if (line_is_kw(p, "ENDSCAN")) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

/* ---- Scan forward for RECOVER or END SEQUENCE ----
   kind: 0=find either RECOVER or END SEQUENCE, 1=find only END SEQUENCE */
static int scan_sequence_end(program_t *prog, int from, int kind) {
    int depth = 1;
    int i;
    for (i = from + 1; i < prog->nlines; i++) {
        char line[MAX_LINE_LEN];
        char *p;
        str_copy(line, prog->lines[i], MAX_LINE_LEN);
        prog_preprocess(line, cmd_get_memvar_store());
        p = skip_ws(line);
        if (line_is_kw(p, "BEGIN") && str_imatch(skip_ws(p + 5), "SEQUENCE"))
            depth++;
        else if (line_is_kw(p, "END") && str_imatch(skip_ws(p + 3), "SEQUENCE")) {
            depth--;
            if (depth == 0) return i;
        } else if (kind == 0 && depth == 1 && line_is_kw(p, "RECOVER")) {
            return i;
        }
    }
    return -1;
}

void prog_begin_sequence(void) {
    if (seq_depth >= MAX_SEQUENCE_DEPTH) {
        prog_error(ERR_STACK_OVERFLOW, "SEQUENCE nesting overflow");
        return;
    }
    seq_stack[seq_depth].pc = state.pc;
    seq_stack[seq_depth].call_depth = state.call_depth;
    seq_stack[seq_depth].loop_depth = state.loop_depth;
    seq_stack[seq_depth].prog = state.current_prog;
    seq_depth++;
    state.pc++;
}

void prog_end_sequence(void) {
    if (seq_depth > 0)
        seq_depth--;
    state.pc++;
}

void prog_recover(void) {
    /* During normal flow (no error), skip to END SEQUENCE */
    if (!state.current_prog) { state.pc++; return; }
    int target = scan_sequence_end(state.current_prog, state.pc, 1);
    if (target >= 0)
        state.pc = target + 1;
    else
        state.pc++;
    if (seq_depth > 0)
        seq_depth--;
}

void prog_break(void) {
    if (seq_depth <= 0) {
        prog_error(ERR_SYNTAX, "BREAK without BEGIN SEQUENCE");
        return;
    }
    seq_depth--;
    sequence_frame_t *f = &seq_stack[seq_depth];

    /* Unwind call/loop depth */
    while (state.call_depth > f->call_depth) {
        state.call_depth--;
    }
    state.loop_depth = f->loop_depth;
    state.current_prog = f->prog;

    /* Scan from BEGIN SEQUENCE for RECOVER or END SEQUENCE */
    int target = scan_sequence_end(f->prog, f->pc, 0);
    if (target >= 0) {
        char line[MAX_LINE_LEN];
        char *p;
        str_copy(line, f->prog->lines[target], MAX_LINE_LEN);
        prog_preprocess(line, cmd_get_memvar_store());
        p = skip_ws(line);
        if (line_is_kw(p, "RECOVER"))
            state.pc = target + 1;  /* execute lines after RECOVER */
        else
            state.pc = target + 1;  /* skip past END SEQUENCE */
    } else {
        state.pc = f->pc + 1;
    }
}

/* ---- Save PRIVATE variables ---- */
static void save_private(call_frame_t *frame, const char *name) {
    memvar_store_t *store = cmd_get_memvar_store();
    value_t val;
    int i;

    if (frame->private_count >= 64) return;

    /* Check if already saved */
    for (i = 0; i < frame->private_count; i++) {
        if (str_icmp(frame->private_vars[i], name) == 0)
            return;
    }

    str_copy(frame->private_vars[frame->private_count], name, MEMVAR_NAMELEN);
    str_upper(frame->private_vars[frame->private_count]);

    if (memvar_find(store, name, &val) == 0) {
        frame->saved_vals[frame->private_count] = val;
        frame->saved_valid[frame->private_count] = 1;
    } else {
        frame->saved_valid[frame->private_count] = 0;
    }
    frame->private_count++;
}

/* ---- Restore PRIVATE variables on RETURN ---- */
static void restore_privates(call_frame_t *frame) {
    memvar_store_t *store = cmd_get_memvar_store();
    int i;

    for (i = 0; i < frame->private_count; i++) {
        if (frame->saved_valid[i]) {
            memvar_set(store, frame->private_vars[i], &frame->saved_vals[i]);
        } else {
            memvar_release(store, frame->private_vars[i]);
        }
    }
}

/* ---- Push call frame ---- */
static int push_frame(program_t *new_prog, int start_line) {
    call_frame_t *frame;

    if (state.call_depth >= MAX_CALL_DEPTH) {
        prog_error(ERR_STACK_OVERFLOW, "Call stack overflow");
        return -1;
    }

    frame = &state.call_stack[state.call_depth];
    memset(frame, 0, sizeof(call_frame_t));
    frame->caller_prog = state.current_prog;
    frame->caller_line = state.pc;
    frame->prog = new_prog;
    frame->return_line = start_line;
    frame->private_count = 0;

    state.call_depth++;
    state.current_prog = new_prog;
    state.pc = start_line;

    /* Update memvar scope depth so new variables are tagged to this frame */
    cmd_get_memvar_store()->current_depth = state.call_depth;

    return 0;
}

/* ---- Pop call frame ---- */
static void pop_frame(void) {
    call_frame_t *frame;
    memvar_store_t *store = cmd_get_memvar_store();

    if (state.call_depth <= 0) return;

    state.call_depth--;
    frame = &state.call_stack[state.call_depth];

    /* Snapshot by-reference parameter values before restore clobbers them */
    value_t ref_vals[MAX_FUNC_ARGS];
    int i;
    for (i = 0; i < frame->with_argc; i++) {
        if (frame->with_ref_names[i][0] && frame->param_names[i][0]) {
            memvar_find(store, frame->param_names[i], &ref_vals[i]);
        }
    }

    /* Restore PRIVATE variables */
    restore_privates(frame);

    /* Restore memvar scope depth to caller's level */
    store->current_depth = state.call_depth;

    /* Write back by-reference parameters to caller's variables */
    for (i = 0; i < frame->with_argc; i++) {
        if (frame->with_ref_names[i][0] && frame->param_names[i][0]) {
            memvar_set(store, frame->with_ref_names[i], &ref_vals[i]);
        }
    }

    if (frame->caller_prog) {
        state.current_prog = frame->caller_prog;
        state.pc = frame->caller_line + 1;
    } else {
        /* Return to interactive mode */
        state.current_prog = NULL;
        state.running = 0;
    }

    /* If the program we returned from was the top-level DO and no caller,
       free it (but not the shared procedure library) */
    if (state.call_depth == 0 && !frame->caller_prog) {
        if (frame->prog != procedure_file)
            prog_free(frame->prog);
    }

    /* Synchronous call: stop prog_run when we return to the target depth */
    if (sync_call_active && state.call_depth <= sync_call_depth) {
        state.running = 0;
    }
}

/* ---- Execute program lines ---- */
static void prog_run(void) {
    int run_slot = state.call_depth;
    char *line;
    char *prev;
    char *line2;

    if (run_slot < 0) run_slot = 0;
    if (run_slot >= MAX_CALL_DEPTH + 2) run_slot = MAX_CALL_DEPTH + 1;
    line = run_line_buf[run_slot];
    prev = run_prev_buf[run_slot];
    line2 = run_line2_buf[run_slot];

    state.running = 1;

    for (;;) {
    while (state.running && state.current_prog && state.pc < state.current_prog->nlines) {
        char *p;
        int quit;

        str_copy(line, state.current_prog->lines[state.pc], MAX_LINE_LEN);

        /* Preprocess: comments and macro substitution */
        prog_preprocess(line, cmd_get_memvar_store());

        p = skip_ws(line);

        /* SET ESCAPE: check for Esc keypress */
        if (cmd_get_escape() && screen_check_escape()) {
            printf("*** Interrupted\n");
            state.running = 0;
            break;
        }

        /* SET ECHO: show command line during program execution */
        if (cmd_get_echo() && *p != '\0' && *p != '*')
            printf("[%s:%d] %s\n", state.current_prog->filename, state.pc + 1, p);

        /* Full-line comment: * or NOTE */
        if (*p == '*' || str_imatch(p, "NOTE")) {
            state.pc++;
            continue;
        }

        /* Empty line after preprocessing */
        if (*p == '\0') {
            state.pc++;
            continue;
        }

        /* Skip PROCEDURE/FUNCTION definitions during normal execution.
           Exception: if this is the first executable line in the file
           (Clipper convention — first PROCEDURE is the entry point),
           just skip the declaration line and execute the body. */
        if (line_is_kw(p, "PROCEDURE") || line_is_kw(p, "FUNCTION")) {
            if (state.call_depth == 0) {
                /* Check if all prior lines were comments/blank */
                int all_comments = 1;
                int j;
                for (j = 0; j < state.pc; j++) {
                    char *pp;
                    str_copy(prev, state.current_prog->lines[j], MAX_LINE_LEN);
                    pp = skip_ws(prev);
                    if (*pp != '\0' && *pp != '*' && !str_imatch(pp, "NOTE")) {
                        all_comments = 0;
                        break;
                    }
                }
                if (all_comments) {
                    /* Entry point — skip PROCEDURE line, execute body */
                    state.pc++;
                    continue;
                }
            }
            /* Skip to next PROCEDURE/FUNCTION or end of file */
            int i;
            for (i = state.pc + 1; i < state.current_prog->nlines; i++) {
                char *lp;
                str_copy(line2, state.current_prog->lines[i], MAX_LINE_LEN);
                prog_preprocess(line2, cmd_get_memvar_store());
                lp = skip_ws(line2);
                if (line_is_kw(lp, "PROCEDURE") || line_is_kw(lp, "FUNCTION"))
                    break;
            }
            /* If we hit another PROCEDURE, stop there.
               If we hit end of file, we're done. */
            if (i >= state.current_prog->nlines) {
                /* End of program */
                if (state.call_depth > 0)
                    pop_frame();
                else {
                    state.running = 0;
                }
                continue;
            }
            state.pc = i;
            continue;
        }

        /* Handle IF/ELSE/ENDIF/DO CASE/CASE/OTHERWISE/ENDCASE skip mode */
        if (if_skip > 0) {
            /* Counting nesting while skipping */
            if (line_is_kw(p, "IF")) {
                if_skip++;
            } else if (line_is_kw(p, "ENDIF")) {
                if_skip--;
                if (if_skip == 0) {
                    /* We've found matching ENDIF */
                    if (if_depth > 0) if_depth--;
                }
            } else if (if_skip == 1 && line_is_kw(p, "ELSE")) {
                /* At our nesting level, found ELSE */
                if (if_depth > 0 && !if_done[if_depth - 1]) {
                    if_skip = 0; /* start executing ELSE branch */
                    if_done[if_depth - 1] = 1;
                }
            } else if (str_imatch(p, "TEXT")) {
                /* Skip TEXT...ENDTEXT block */
                state.pc++;
                while (state.pc < state.current_prog->nlines) {
                    char *tp = skip_ws(state.current_prog->lines[state.pc]);
                    if (str_imatch(tp, "ENDTEXT")) break;
                    state.pc++;
                }
            }
            state.pc++;
            continue;
        }

        if (case_skip) {
            /* Inside DO CASE, skipping to next CASE/OTHERWISE/ENDCASE */
            if (line_is_do_kw(p, "CASE")) {
                /* nested DO CASE - skip the whole thing */
                int target = scan_endcase(state.current_prog, state.pc);
                if (target >= 0) {
                    state.pc = target + 1;
                    continue;
                }
            } else if (str_imatch(p, "TEXT")) {
                /* Skip TEXT...ENDTEXT block */
                state.pc++;
                while (state.pc < state.current_prog->nlines) {
                    char *tp = skip_ws(state.current_prog->lines[state.pc]);
                    if (str_imatch(tp, "ENDTEXT")) break;
                    state.pc++;
                }
                state.pc++;
                continue;
            } else if (line_is_kw(p, "CASE")) {
                if (!case_done) {
                    /* Evaluate this CASE condition */
                    char *cond = skip_ws(p + 4);
                    value_t val;
                    expr_ctx_t *ctx = cmd_get_expr_ctx();
                    if (expr_eval_str(ctx, cond, &val) == 0 &&
                        val.type == VAL_LOGIC && val.logic) {
                        case_skip = 0;
                        case_done = 1;
                        state.pc++;
                        continue;
                    }
                }
                state.pc++;
                continue;
            } else if (line_is_kw(p, "OTHERWISE")) {
                if (!case_done) {
                    case_skip = 0;
                    case_done = 1;
                }
                state.pc++;
                continue;
            } else if (line_is_kw(p, "ENDCASE")) {
                case_skip = 0;
                case_active = 0;
                case_done = 0;
                state.pc++;
                continue;
            }
            state.pc++;
            continue;
        }

        /* Normal execution */
        quit = prog_execute_line(line);
        if (quit) {
            state.running = 0;
            return;
        }

        /* If prog_execute_line didn't change PC (control flow commands set it),
           advance to next line. For normal commands, we need to advance. */
        /* Control flow commands will set state.pc themselves, but normal
           commands return and we need to advance. We handle this by noting
           that control flow commands set state.pc, and we only advance here
           if the line was a normal command. Actually, let's just always
           let the control flow code manage PC and advance for non-control. */
    }

    /* End of program - pop frame if needed */
    if (state.running && state.current_prog &&
        state.pc >= state.current_prog->nlines) {
        if (state.call_depth > 0) {
            pop_frame();
            if (state.running && state.current_prog)
                continue; /* continue with caller (was recursive tail call) */
        } else {
            state.running = 0;
            if (state.current_prog) {
                /* Don't free the procedure library — it's shared */
                if (state.current_prog != procedure_file)
                    prog_free(state.current_prog);
                state.current_prog = NULL;
            }
        }
    }
    break;
    } /* for (;;) */
}

/* ---- DO command: DO <file> or DO WHILE <cond> ---- */
void prog_do(const char *arg) {
    const char *p = skip_ws(arg);

    if (str_imatch(p, "WHILE")) {
        /* DO WHILE <condition> */
        char *cond = skip_ws(p + 5);
        value_t val;
        expr_ctx_t *ctx = cmd_get_expr_ctx();
        const char *ast_err;
        ast_node_t *cond_ast;

        if (!state.running) {
            printf("DO WHILE not allowed in interactive mode.\n");
            return;
        }

        /* Compile condition to AST (skip caching for macro expressions) */
        cond_ast = NULL;
        if (!strchr(cond, '&'))
            cond_ast = ast_compile(cond, cmd_get_memvar_store(), &ast_err);

        /* Evaluate condition (use AST if available) */
        if (cond_ast) {
            if (ast_eval(cond_ast, ctx, &val) != 0) {
                if (ctx->error) prog_error(expr_error_code(ctx->error), ctx->error);
                ast_free(cond_ast);
                state.pc++;
                return;
            }
        } else {
            if (ast_eval_dynamic(cond, ctx, &val) != 0) {
                if (ctx->error) prog_error(expr_error_code(ctx->error), ctx->error);
                state.pc++;
                return;
            }
        }

        if (val.type == VAL_LOGIC && val.logic) {
            /* Push loop entry */
            if (state.loop_depth >= MAX_LOOP_DEPTH) {
                printf("Loop nesting too deep.\n");
                ast_free(cond_ast);
                state.pc++;
                return;
            }
            state.loop_stack[state.loop_depth].start_line = state.pc;
            state.loop_stack[state.loop_depth].type = 0; /* DO WHILE */
            str_copy(state.loop_stack[state.loop_depth].condition, cond,
                     sizeof(state.loop_stack[state.loop_depth].condition));
            state.loop_stack[state.loop_depth].condition_ast = cond_ast;
            state.loop_stack[state.loop_depth].if_depth_at_entry = if_depth;
            state.loop_depth++;
            state.pc++;
        } else {
            /* Skip to matching ENDDO */
            int target = scan_enddo(state.current_prog, state.pc);
            ast_free(cond_ast);
            if (target < 0) {
                printf("ENDDO not found.\n");
                state.pc++;
                return;
            }
            state.pc = target + 1;
        }
        return;
    }

    if (str_imatch(p, "CASE")) {
        /* DO CASE */
        prog_do_case();
        return;
    }

    /* DO <file> or DO <procedure> [IN <file>] [WITH arg1, arg2, ...] */
    {
        char name[64];
        char in_file[64];
        int i = 0;
        program_t *prog;
        int proc_line;
        value_t with_args[MAX_FUNC_ARGS];
        int with_argc = 0;

        while (*p && *p != ' ' && *p != '\t' && i < 63)
            name[i++] = *p++;
        name[i] = '\0';

        /* Parse optional IN clause */
        p = skip_ws(p);
        in_file[0] = '\0';
        if (str_imatch(p, "IN")) {
            int j = 0;
            p = skip_ws(p + 2);
            while (*p && *p != ' ' && *p != '\t' && j < 63)
                in_file[j++] = *p++;
            in_file[j] = '\0';
            p = skip_ws(p);
        }

        /* Parse optional WITH clause */
        char ref_names[MAX_FUNC_ARGS][MEMVAR_NAMELEN];
        memset(ref_names, 0, sizeof(ref_names));
        if (str_imatch(p, "WITH")) {
            expr_ctx_t *ctx = cmd_get_expr_ctx();
            p = skip_ws(p + 4);
            while (*p && with_argc < MAX_FUNC_ARGS) {
                value_t val;
                /* Detect simple variable names for pass-by-reference.
                   In dBase III, DO proc WITH varname passes by reference,
                   while DO proc WITH (expr) or literals pass by value. */
                const char *ap = p;
                if ((*ap >= 'A' && *ap <= 'Z') || (*ap >= 'a' && *ap <= 'z') || *ap == '_') {
                    const char *q = ap;
                    while ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') ||
                           (*q >= '0' && *q <= '9') || *q == '_') q++;
                    const char *after = skip_ws(q);
                    if (*after == ',' || *after == '\0') {
                        /* Simple identifier — pass by reference */
                        int len = (int)(q - ap);
                        if (len >= MEMVAR_NAMELEN) len = MEMVAR_NAMELEN - 1;
                        memcpy(ref_names[with_argc], ap, len);
                        ref_names[with_argc][len] = '\0';
                        str_upper(ref_names[with_argc]);
                    }
                }
                if (expr_eval(ctx, &p, &val) != 0) {
                    if (ctx->error) prog_error(expr_error_code(ctx->error), ctx->error);
                    break;
                }
                with_args[with_argc++] = val;
                p = skip_ws(p);
                if (*p == ',') { p++; p = skip_ws(p); }
                else break;
            }
        }

        /* If IN <file> specified, load that file and search for procedure there first */
        if (in_file[0]) {
            program_t *in_prog = prog_load(in_file);
            if (in_prog) {
                proc_line = find_procedure(in_prog, name);
                if (proc_line >= 0) {
                    if (state.running) {
                        if (push_frame(in_prog, proc_line + 1) < 0) {
                            prog_free(in_prog);
                            return;
                        }
                        if (with_argc > 0 && state.call_depth > 0) {
                            call_frame_t *frame = &state.call_stack[state.call_depth - 1];
                            frame->with_argc = with_argc;
                            memcpy(frame->with_args, with_args, with_argc * sizeof(value_t));
                            memcpy(frame->with_ref_names, ref_names, sizeof(ref_names));
                        }
                        return;
                    }
                    /* Interactive DO — start execution at the procedure */
                    state.current_prog = in_prog;
                    state.pc = proc_line + 1;
                    state.call_depth = 0;
                    state.loop_depth = 0;
                    if_skip = 0;
                    if_depth = 0;
                    case_active = 0;
                    case_done = 0;
                    case_skip = 0;
                    if (with_argc > 0) {
                        if (push_frame(in_prog, proc_line + 1) == 0) {
                            call_frame_t *frame = &state.call_stack[state.call_depth - 1];
                            frame->with_argc = with_argc;
                            memcpy(frame->with_args, with_args, with_argc * sizeof(value_t));
                            memcpy(frame->with_ref_names, ref_names, sizeof(ref_names));
                            frame->caller_prog = NULL;
                        }
                    }
                    prog_run();
                    return;
                }
                prog_free(in_prog);
            }
        }

        /* First check if it's a procedure in the current program */
        if (state.running && state.current_prog) {
            proc_line = find_procedure(state.current_prog, name);
            if (proc_line >= 0) {
                /* Call procedure within same program */
                if (push_frame(state.current_prog, proc_line + 1) < 0)
                    return;
                /* Store WITH args in the new frame */
                if (with_argc > 0 && state.call_depth > 0) {
                    call_frame_t *frame = &state.call_stack[state.call_depth - 1];
                    frame->with_argc = with_argc;
                    memcpy(frame->with_args, with_args, with_argc * sizeof(value_t));
                    memcpy(frame->with_ref_names, ref_names, sizeof(ref_names));
                }
                return;
            }
        }

        /* Check SET PROCEDURE TO file */
        if (procedure_file) {
            proc_line = find_procedure(procedure_file, name);
            if (proc_line >= 0) {
                if (state.running) {
                    /* Nested call from within a running program */
                    if (push_frame(procedure_file, proc_line + 1) < 0)
                        return;
                    if (with_argc > 0 && state.call_depth > 0) {
                        call_frame_t *frame = &state.call_stack[state.call_depth - 1];
                        frame->with_argc = with_argc;
                        memcpy(frame->with_args, with_args, with_argc * sizeof(value_t));
                        memcpy(frame->with_ref_names, ref_names, sizeof(ref_names));
                    }
                    return;
                }
                /* Interactive DO — start execution at the procedure */
                state.current_prog = procedure_file;
                state.pc = proc_line + 1;
                state.call_depth = 0;
                state.loop_depth = 0;
                if_skip = 0;
                if_depth = 0;
                case_active = 0;
                case_done = 0;
                case_skip = 0;
                if (with_argc > 0) {
                    if (push_frame(procedure_file, proc_line + 1) == 0) {
                        call_frame_t *frame = &state.call_stack[state.call_depth - 1];
                        frame->with_argc = with_argc;
                        memcpy(frame->with_args, with_args, with_argc * sizeof(value_t));
                        memcpy(frame->with_ref_names, ref_names, sizeof(ref_names));
                        frame->caller_prog = NULL;
                    }
                }
                prog_run();
                return;
            }
        }

        /* Load external PRG file */
        prog = prog_load(name);
        if (!prog) return;

        if (state.running) {
            /* Nested DO from within a program */
            if (push_frame(prog, 0) < 0) {
                prog_free(prog);
                return;
            }
            /* Store WITH args in the new frame */
            if (with_argc > 0 && state.call_depth > 0) {
                call_frame_t *frame = &state.call_stack[state.call_depth - 1];
                frame->with_argc = with_argc;
                memcpy(frame->with_args, with_args, with_argc * sizeof(value_t));
                memcpy(frame->with_ref_names, ref_names, sizeof(ref_names));
            }
        } else {
            /* Interactive DO - push a frame so PARAMETERS can find WITH args */
            state.current_prog = prog;
            state.pc = 0;
            state.call_depth = 0;
            state.loop_depth = 0;
            if_skip = 0;
            if_depth = 0;
            case_active = 0;
            case_done = 0;
            case_skip = 0;
            if (with_argc > 0) {
                /* Push a frame at depth 0 for the interactive DO */
                if (push_frame(prog, 0) == 0) {
                    call_frame_t *frame = &state.call_stack[state.call_depth - 1];
                    frame->with_argc = with_argc;
                    memcpy(frame->with_args, with_args, with_argc * sizeof(value_t));
                    memcpy(frame->with_ref_names, ref_names, sizeof(ref_names));
                    frame->caller_prog = NULL;
                }
            }
            prog_run();
        }
    }
}

/* ---- Synchronous procedure call (safe from command handlers) ---- */
void prog_call_sync(const char *procname) {
    int saved_call_depth = state.call_depth;
    int saved_pc = state.pc;
    program_t *saved_prog = state.current_prog;
    int saved_running = state.running;
    int saved_sync_active = sync_call_active;
    int saved_sync_depth = sync_call_depth;

    sync_call_active = 1;
    sync_call_depth = saved_call_depth;

    prog_do(procname);

    /* If prog_do pushed a frame, run the procedure to completion */
    if (state.call_depth > saved_call_depth && state.running) {
        prog_run();
    }

    /* Restore caller's execution state */
    state.pc = saved_pc;
    state.current_prog = saved_prog;
    state.running = saved_running;
    sync_call_active = saved_sync_active;
    sync_call_depth = saved_sync_depth;
}

/* ---- IF ---- */
void prog_if(const char *arg) {
    value_t val;
    expr_ctx_t *ctx = cmd_get_expr_ctx();

    if (!state.running) {
        printf("IF not allowed in interactive mode.\n");
        return;
    }

    if (expr_eval_str(ctx, arg, &val) != 0) {
        if (ctx->error) prog_error(expr_error_code(ctx->error), ctx->error);
        state.pc++;
        return;
    }

    if_depth++;
    if (if_depth > MAX_IF_DEPTH) {
        printf("IF nesting too deep.\n");
        if_depth--;
        state.pc++;
        return;
    }

    if (val.type == VAL_LOGIC && val.logic) {
        /* Condition true, execute IF block */
        if_done[if_depth - 1] = 1;
        state.pc++;
    } else {
        /* Condition false, skip to ELSE or ENDIF */
        if_done[if_depth - 1] = 0;
        if_skip = 1;
        state.pc++;
    }
}

/* ---- ELSE ---- */
void prog_else(void) {
    if (!state.running) {
        printf("ELSE without IF.\n");
        return;
    }

    /* We were executing the IF-true block, now skip to ENDIF */
    if_skip = 1;
    /* Mark as done so ELSE branch won't also execute */
    if (if_depth > 0)
        if_done[if_depth - 1] = 1;
    state.pc++;
}

/* ---- ENDIF ---- */
void prog_endif(void) {
    if (!state.running) {
        printf("ENDIF without IF.\n");
        return;
    }

    if (if_depth > 0)
        if_depth--;
    state.pc++;
}

/* ---- DO CASE ---- */
void prog_do_case(void) {
    if (!state.running) {
        printf("DO CASE not allowed in interactive mode.\n");
        return;
    }

    case_active = 1;
    case_done = 0;
    case_skip = 1; /* skip until first CASE */
    state.pc++;
}

/* ---- CASE ---- */
void prog_case(const char *arg) {
    if (!state.running || !case_active) {
        printf("CASE without DO CASE.\n");
        return;
    }

    if (case_done) {
        /* A previous CASE was taken, skip to ENDCASE */
        case_skip = 1;
        state.pc++;
        return;
    }

    {
        value_t val;
        expr_ctx_t *ctx = cmd_get_expr_ctx();
        if (expr_eval_str(ctx, arg, &val) != 0) {
            if (ctx->error) prog_error(expr_error_code(ctx->error), ctx->error);
            state.pc++;
            return;
        }

        if (val.type == VAL_LOGIC && val.logic) {
            case_done = 1;
            case_skip = 0;
            state.pc++;
        } else {
            case_skip = 1;
            state.pc++;
        }
    }
}

/* ---- OTHERWISE ---- */
void prog_otherwise(void) {
    if (!state.running || !case_active) {
        printf("OTHERWISE without DO CASE.\n");
        return;
    }

    if (case_done) {
        case_skip = 1;
    } else {
        case_done = 1;
        case_skip = 0;
    }
    state.pc++;
}

/* ---- ENDCASE ---- */
void prog_endcase(void) {
    if (!state.running) {
        printf("ENDCASE without DO CASE.\n");
        return;
    }

    case_active = 0;
    case_done = 0;
    case_skip = 0;
    state.pc++;
}

/* ---- ENDDO ---- */
void prog_enddo(void) {
    expr_ctx_t *ctx;
    value_t val;

    if (!state.running) {
        printf("ENDDO without DO WHILE.\n");
        return;
    }

    if (state.loop_depth <= 0) {
        printf("ENDDO without DO WHILE.\n");
        state.pc++;
        return;
    }

    /* Re-evaluate condition (use AST if available) */
    ctx = cmd_get_expr_ctx();
    {
        loop_entry_t *loop = &state.loop_stack[state.loop_depth - 1];
        int eval_ok;
        if (loop->condition_ast)
            eval_ok = (ast_eval(loop->condition_ast, ctx, &val) == 0);
        else
            eval_ok = (ast_eval_dynamic(loop->condition, ctx, &val) == 0);

        if (!eval_ok) {
            if (ctx->error) prog_error(expr_error_code(ctx->error), ctx->error);
            ast_free(loop->condition_ast);
            loop->condition_ast = NULL;
            state.loop_depth--;
            state.pc++;
            return;
        }
    }

    if (val.type == VAL_LOGIC && val.logic) {
        /* Loop back to start (line after DO WHILE) */
        state.pc = state.loop_stack[state.loop_depth - 1].start_line + 1;
    } else {
        /* Exit loop — free AST */
        loop_entry_t *loop = &state.loop_stack[state.loop_depth - 1];
        ast_free(loop->condition_ast);
        loop->condition_ast = NULL;
        clause_free_ast(&loop->scan_clause);
        state.loop_depth--;
        state.pc++;
    }
}

/* ---- LOOP ---- */
void prog_loop(void) {
    loop_entry_t *loop;

    if (!state.running || state.loop_depth <= 0) {
        printf("LOOP without DO WHILE/FOR.\n");
        return;
    }

    loop = &state.loop_stack[state.loop_depth - 1];

    /* Unwind IF nesting back to loop entry level */
    if_depth = loop->if_depth_at_entry;
    if_skip = 0;

    if (loop->type == 1) {
        /* FOR loop: jump to the NEXT line so it handles increment/check */
        int target = scan_next(state.current_prog, loop->start_line);
        if (target >= 0) {
            state.pc = target;
        } else {
            printf("NEXT not found.\n");
            state.pc++;
        }
    } else if (loop->type == 2) {
        /* SCAN loop: jump to ENDSCAN */
        int target = scan_endscan(state.current_prog, loop->start_line);
        if (target >= 0) {
            state.pc = target;
        } else {
            printf("ENDSCAN not found.\n");
            state.pc++;
        }
    } else {
        /* DO WHILE loop: pop and re-evaluate at DO WHILE line */
        state.loop_depth--;
        state.pc = state.loop_stack[state.loop_depth].start_line;
    }
}

/* ---- EXIT (from loop) ---- */
void prog_exit_loop(void) {
    loop_entry_t *loop;
    int target;

    if (!state.running || state.loop_depth <= 0) {
        printf("EXIT without DO WHILE/FOR.\n");
        return;
    }

    loop = &state.loop_stack[state.loop_depth - 1];

    /* Unwind IF nesting back to loop entry level */
    if_depth = loop->if_depth_at_entry;
    if_skip = 0;

    if (loop->type == 1) {
        target = scan_next(state.current_prog, loop->start_line);
    } else if (loop->type == 2) {
        target = scan_endscan(state.current_prog, loop->start_line);
    } else {
        target = scan_enddo(state.current_prog, loop->start_line);
    }

    /* Free ASTs when exiting loop */
    ast_free(loop->condition_ast);
    loop->condition_ast = NULL;
    clause_free_ast(&loop->scan_clause);

    state.loop_depth--;

    if (target >= 0) {
        state.pc = target + 1;
    } else {
        const char *name = "ENDDO";
        if (loop->type == 1) name = "NEXT";
        else if (loop->type == 2) name = "ENDSCAN";
        printf("%s not found.\n", name);
        state.pc++;
    }
}

/* ---- FOR ---- */
void prog_for(const char *arg) {
    const char *p = skip_ws(arg);
    char varname[MEMVAR_NAMELEN];
    value_t start_val, end_val, step_val;
    expr_ctx_t *ctx = cmd_get_expr_ctx();
    memvar_store_t *store = cmd_get_memvar_store();
    int i = 0;

    if (!state.running) {
        printf("FOR not allowed in interactive mode.\n");
        return;
    }

    /* Parse variable name */
    while (is_ident_char(*p) && i < MEMVAR_NAMELEN - 1)
        varname[i++] = *p++;
    varname[i] = '\0';

    p = skip_ws(p);
    if (*p != '=') {
        printf("Syntax error in FOR.\n");
        state.pc++;
        return;
    }
    p++;

    /* Parse start expression */
    p = skip_ws(p);
    if (expr_eval(ctx, &p, &start_val) != 0 || start_val.type != VAL_NUM) {
        printf("FOR requires numeric expressions.\n");
        state.pc++;
        return;
    }

    /* Expect TO */
    p = skip_ws(p);
    if (!str_imatch(p, "TO")) {
        printf("Missing TO in FOR.\n");
        state.pc++;
        return;
    }
    p = skip_ws(p + 2);

    /* Parse end expression */
    if (expr_eval(ctx, &p, &end_val) != 0 || end_val.type != VAL_NUM) {
        printf("FOR requires numeric expressions.\n");
        state.pc++;
        return;
    }

    /* Optional STEP */
    step_val = val_num(1.0);
    p = skip_ws(p);
    if (str_imatch(p, "STEP")) {
        p = skip_ws(p + 4);
        if (expr_eval(ctx, &p, &step_val) != 0 || step_val.type != VAL_NUM) {
            printf("STEP requires numeric expression.\n");
            state.pc++;
            return;
        }
    }

    /* Set loop variable to start value */
    memvar_set(store, varname, &start_val);

    /* Check if loop should execute at all */
    if ((step_val.num > 0 && start_val.num > end_val.num) ||
        (step_val.num < 0 && start_val.num < end_val.num)) {
        int target = scan_next(state.current_prog, state.pc);
        if (target < 0) {
            printf("NEXT not found.\n");
            state.pc++;
            return;
        }
        state.pc = target + 1;
        return;
    }

    /* Push loop entry */
    if (state.loop_depth >= MAX_LOOP_DEPTH) {
        printf("Loop nesting too deep.\n");
        state.pc++;
        return;
    }

    state.loop_stack[state.loop_depth].start_line = state.pc;
    state.loop_stack[state.loop_depth].type = 1; /* FOR */
    state.loop_stack[state.loop_depth].condition[0] = '\0';
    str_copy(state.loop_stack[state.loop_depth].varname, varname, MEMVAR_NAMELEN);
    state.loop_stack[state.loop_depth].end_val = end_val.num;
    state.loop_stack[state.loop_depth].step_val = step_val.num;
    state.loop_stack[state.loop_depth].if_depth_at_entry = if_depth;
    state.loop_depth++;
    state.pc++;
}

/* ---- NEXT ---- */
void prog_next(void) {
    loop_entry_t *loop;
    memvar_store_t *store;
    value_t val;
    int done;

    if (!state.running || state.loop_depth <= 0) {
        printf("NEXT without FOR.\n");
        if (state.running) state.pc++;
        return;
    }

    loop = &state.loop_stack[state.loop_depth - 1];
    if (loop->type != 1) {
        printf("NEXT without FOR.\n");
        state.pc++;
        return;
    }

    /* Increment variable */
    store = cmd_get_memvar_store();
    if (memvar_find(store, loop->varname, &val) != 0 || val.type != VAL_NUM) {
        state.loop_depth--;
        state.pc++;
        return;
    }

    val.num += loop->step_val;
    memvar_set(store, loop->varname, &val);

    /* Check termination */
    if (loop->step_val > 0)
        done = (val.num > loop->end_val);
    else if (loop->step_val < 0)
        done = (val.num < loop->end_val);
    else
        done = 0; /* STEP 0 = infinite loop */

    if (done) {
        state.loop_depth--;
        state.pc++;
    } else {
        state.pc = loop->start_line + 1;
    }
}

/* ---- SCAN [scope] [FOR cond] [WHILE cond] ---- */
void prog_scan(const char *arg) {
    clause_t c;
    lexer_t l;
    uint32_t start, end;
    dbf_t *db = area_get_current() ? &area_get_current()->db : NULL;

    if (!db || !dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    clause_init(&c);
    c.scope.type = SCOPE_ALL_DEFAULT; /* SCAN default is ALL */
    lexer_init_ext(&l, arg, cmd_get_memvar_store());
    if (parse_clauses(&l, &c) < 0) return;

    /* Compile clause ASTs for scan loop */
    clause_compile(&c, cmd_get_memvar_store());

    if (!c.has_scope) {
        if (c.for_cond[0]) c.scope.type = SCOPE_ALL;
        else if (c.scope.type == SCOPE_ALL_DEFAULT) c.scope.type = SCOPE_ALL;
        else if (c.scope.type == SCOPE_DEFAULT) {
            c.scope.type = SCOPE_NEXT;
            c.scope.count = 1;
        }
    }

    if (scope_bounds(db, &c.scope, &start, &end) < 0) {
        clause_free_ast(&c);
        prog_error(ERR_SYNTAX, "Invalid scope");
        return;
    }

    if (state.loop_depth >= MAX_LOOP_DEPTH) {
        clause_free_ast(&c);
        prog_error(ERR_SYNTAX, "Loop nesting overflow");
        return;
    }

    {
    loop_entry_t *loop = &state.loop_stack[state.loop_depth++];
    loop->start_line = state.pc;
    loop->type = 2; /* SCAN */
    loop->if_depth_at_entry = if_depth;
    loop->scan_clause = c;
    loop->scan_end = end;

    /* Position to start of scan */
    if (start < 1 || start > end || start > db->record_count) {
        clause_free_ast(&loop->scan_clause);
        state.loop_depth--; /* Exit SCAN */
        {
        int target = scan_endscan(state.current_prog, state.pc);
        if (target >= 0) state.pc = target + 1;
        else state.pc++;
        }
        return;
    }
    dbf_read_record(db, start);
    loop->scan_current = start;

    /* Advance to first matching record */
    while (loop->scan_current <= loop->scan_end) {
        int stop_scan = 0;
        int matched = scan_record_matches(loop, db, &stop_scan);
        if (matched < 0) {
            clause_free_ast(&loop->scan_clause);
            state.loop_depth--;
            {
                int target = scan_endscan(state.current_prog, state.pc);
                if (target >= 0) state.pc = target + 1;
                else state.pc++;
            }
            return;
        }
        if (stop_scan) {
            clause_free_ast(&loop->scan_clause);
            state.loop_depth--; /* Exit SCAN */
            {
            int target = scan_endscan(state.current_prog, state.pc);
            if (target >= 0) state.pc = target + 1;
            else state.pc++;
            }
            return;
        }
        if (matched) break;

        loop->scan_current++;
        if (loop->scan_current <= loop->scan_end)
            dbf_read_record(db, loop->scan_current);
    }

    if (loop->scan_current > loop->scan_end || loop->scan_current > db->record_count) {
        clause_free_ast(&loop->scan_clause);
        state.loop_depth--; /* Exit SCAN */
        {
        int target = scan_endscan(state.current_prog, state.pc);
        if (target >= 0) state.pc = target + 1;
        else state.pc++;
        }
    } else {
        state.pc++;
    }
    }
}

void prog_endscan(void) {
    if (state.loop_depth == 0 || state.loop_stack[state.loop_depth - 1].type != 2) {
        /* ENDSCAN without SCAN - just advance */
        state.pc++;
        return;
    }

    {
        loop_entry_t *loop = &state.loop_stack[state.loop_depth - 1];
        dbf_t *db = area_get_current() ? &area_get_current()->db : NULL;

        if (!db || !dbf_is_open(db)) {
            clause_free_ast(&loop->scan_clause);
            state.loop_depth--;
            state.pc++;
            return;
        }

        /* Advance to next record */
        loop->scan_current++;
        if (loop->scan_current <= loop->scan_end)
            dbf_read_record(db, loop->scan_current);

        /* Search for next matching record */
        while (loop->scan_current <= loop->scan_end) {
            int stop_scan = 0;
            int matched = scan_record_matches(loop, db, &stop_scan);
            if (matched < 0) {
                clause_free_ast(&loop->scan_clause);
                state.loop_depth--;
                state.pc++;
                return;
            }
            if (stop_scan) {
                clause_free_ast(&loop->scan_clause);
                state.loop_depth--;
                state.pc++;
                return;
            }
            if (matched) break;

            loop->scan_current++;
            if (loop->scan_current <= loop->scan_end)
                dbf_read_record(db, loop->scan_current);
        }

        if (loop->scan_current > loop->scan_end || loop->scan_current > db->record_count) {
            clause_free_ast(&loop->scan_clause);
            state.loop_depth--; /* Exit SCAN */
            state.pc++;
        } else {
            state.pc = loop->start_line + 1; /* Back to first line of body */
        }
    }
}

/* ---- RETURN ---- */
void prog_return(const char *arg) {
    if (!state.running) {
        printf("RETURN not in program.\n");
        return;
    }

    /* RETURN <expr> for FUNCTION — evaluate the return value */
    if (arg && *arg) {
        expr_ctx_t *ctx = cmd_get_expr_ctx();
        value_t val;
        if (expr_eval_str(ctx, arg, &val) == 0) {
            udf_return_val = val;
        }
    }

    if (state.call_depth > 0) {
        /* If this is a UDF call returning to the UDF entry depth, stop */
        if (udf_active && state.call_depth <= udf_depth) {
            pop_frame();
            state.running = 0;
            return;
        }
        pop_frame();
    } else {
        /* Top-level RETURN = end program */
        state.running = 0;
        if (state.current_prog) {
            if (state.current_prog != procedure_file)
                prog_free(state.current_prog);
            state.current_prog = NULL;
        }
    }
}

/* ---- PROCEDURE ---- */
void prog_procedure(lexer_t *l) {
    (void)l;
    /* During normal execution, skip past procedure body to next PROCEDURE or EOF */
    if (!state.running) {
        printf("PROCEDURE not allowed in interactive mode.\n");
        return;
    }
    /* This is handled in prog_run's main loop */
    state.pc++;
}

/* ---- PARAMETERS ---- */
void prog_parameters(lexer_t *l) {
    memvar_store_t *store = cmd_get_memvar_store();
    call_frame_t *frame = (state.call_depth > 0) ?
        &state.call_stack[state.call_depth - 1] : NULL;
    char name[MEMVAR_NAMELEN];
    int param_idx = 0;

    if (!state.running) {
        printf("PARAMETERS not allowed in interactive mode.\n");
        return;
    }

    while (l->current.type == TOK_IDENT) {
        str_copy(name, l->current.text, sizeof(name));
        str_upper(name);

        if (lex_is_reserved(name)) {
            char err[128];
            snprintf(err, sizeof(err), "%s is a reserved keyword", name);
            prog_error(ERR_SYNTAX, err);
            return;
        }
        /* Record param name for by-reference writeback */
        if (frame && param_idx < MAX_FUNC_ARGS)
            str_copy(frame->param_names[param_idx], name, MEMVAR_NAMELEN);
        /* Save current value as PRIVATE */
        if (frame) {
            save_private(frame, name);
        }
        /* Bind from WITH argument if available, otherwise NIL */
        if (frame && param_idx < frame->with_argc) {
            memvar_set(store, name, &frame->with_args[param_idx]);
        } else {
            value_t nil = val_nil();
            memvar_set(store, name, &nil);
        }
        param_idx++;

        lex_next(l);
        if (l->current.type == TOK_COMMA) {
            lex_next(l);
        } else {
            break;
        }
    }

    state.pc++;
}

/* ---- PRIVATE ---- */
void prog_private(lexer_t *l) {
    memvar_store_t *store = cmd_get_memvar_store();
    char name[MEMVAR_NAMELEN];

    if (!state.running || state.call_depth <= 0) {
        /* In interactive mode or top level, PRIVATE is ignored */
        while (l->current.type != TOK_EOF) lex_next(l);
        if (state.running) state.pc++;
        return;
    }

    while (l->current.type == TOK_IDENT) {
        str_copy(name, l->current.text, sizeof(name));
        str_upper(name);

        if (lex_is_reserved(name)) {
            char err[128];
            snprintf(err, sizeof(err), "%s is a reserved keyword", name);
            prog_error(ERR_SYNTAX, err);
            return;
        }
        save_private(&state.call_stack[state.call_depth - 1], name);
        /* Release the current value - create fresh */
        memvar_release(store, name);

        lex_next(l);
        if (l->current.type == TOK_COMMA) {
            lex_next(l);
        } else {
            break;
        }
    }

    state.pc++;
}

/* ---- PUBLIC ---- */
void prog_public(lexer_t *l) {
    memvar_store_t *store = cmd_get_memvar_store();
    char name[MEMVAR_NAMELEN];

    while (l->current.type == TOK_IDENT) {
        value_t existing;
        str_copy(name, l->current.text, sizeof(name));
        str_upper(name);

        if (lex_is_reserved(name)) {
            char err[128];
            snprintf(err, sizeof(err), "%s is a reserved keyword", name);
            prog_error(ERR_SYNTAX, err);
            return;
        }
        /* Create variable if it doesn't exist */
        if (memvar_find(store, name, &existing) != 0) {
            value_t v = val_logic(0); /* PUBLIC vars default to .F. */
            memvar_set(store, name, &v);
        }

        lex_next(l);
        if (l->current.type == TOK_COMMA) {
            lex_next(l);
        } else {
            break;
        }
    }

    if (state.running) state.pc++;
}

/* ---- LOCAL ---- */
void prog_local(lexer_t *l) {
    memvar_store_t *store = cmd_get_memvar_store();
    char name[MEMVAR_NAMELEN];
    value_t nil_val = val_nil();

    if (!state.running || state.call_depth <= 0) {
        while (l->current.type != TOK_EOF) lex_next(l);
        if (state.running) state.pc++;
        return;
    }

    while (l->current.type == TOK_IDENT) {
        str_copy(name, l->current.text, sizeof(name));
        str_upper(name);

        if (lex_is_reserved(name)) {
            char err[128];
            snprintf(err, sizeof(err), "%s is a reserved keyword", name);
            prog_error(ERR_SYNTAX, err);
            return;
        }
        /* Save as PRIVATE for cleanup on RETURN */
        save_private(&state.call_stack[state.call_depth - 1], name);
        /* Release any existing var */
        memvar_release(store, name);
        /* Create fresh LOCAL var */
        memvar_set_local(store, name, &nil_val);

        lex_next(l);
        if (l->current.type == TOK_COMMA) {
            lex_next(l);
        } else {
            break;
        }
    }

    state.pc++;
}

/* ---- CANCEL ---- */
void prog_cancel(void) {
    /* Abort all program execution */
    while (state.call_depth > 0) {
        restore_privates(&state.call_stack[state.call_depth - 1]);
        state.call_depth--;
    }
    state.running = 0;
    state.loop_depth = 0;
    if_skip = 0;
    if_depth = 0;
    case_active = 0;
    case_done = 0;
    case_skip = 0;
    if (state.current_prog) {
        prog_free(state.current_prog);
        state.current_prog = NULL;
    }
    printf("*** Cancelled\n");
}

/* ---- Execute one line in program context ---- */
int prog_execute_line(char *line) {
    char *p = skip_ws(line);
    int result;

    /* Check for program-specific commands first */
    if (str_imatch(p, "DO")) {
        prog_do(p + 2);
        return 0;
    }

    if (str_imatch(p, "IF")) {
        prog_if(skip_ws(p + 2));
        return 0;
    }

    if (str_imatch(p, "ELSE")) {
        prog_else();
        return 0;
    }

    if (str_imatch(p, "ENDIF")) {
        prog_endif();
        return 0;
    }

    if (str_imatch(p, "ENDDO")) {
        prog_enddo();
        return 0;
    }

    if (str_imatch(p, "FOR")) {
        prog_for(skip_ws(p + 3));
        return 0;
    }

    if (str_imatch(p, "SCAN")) {
        prog_scan(skip_ws(p + 4));
        return 0;
    }

    if (str_imatch(p, "ENDSCAN")) {
        prog_endscan();
        return 0;
    }

    if (str_imatch(p, "NEXT")) {
        prog_next();
        return 0;
    }

    if (str_imatch(p, "LOOP")) {
        prog_loop();
        return 0;
    }

    /* EXIT in loop context */
    if (str_imatch(p, "EXIT")) {
        if (state.loop_depth > 0) {
            prog_exit_loop();
            return 0;
        }
        /* Otherwise fall through to cmd_execute (QUIT/EXIT) */
    }

    if (str_imatch(p, "RETURN")) {
        prog_return(skip_ws(p + 6));
        return 0;
    }

    if (line_is_kw(p, "FUNCTION")) {
        /* FUNCTION treated same as PROCEDURE during execution — skip body */
        lexer_t l;
        lexer_init_ext(&l, skip_ws(p + 8), cmd_get_memvar_store());
        prog_procedure(&l);
        return 0;
    }

    if (line_is_kw(p, "PROCEDURE")) {
        lexer_t l;
        lexer_init_ext(&l, skip_ws(p + 9), cmd_get_memvar_store());
        prog_procedure(&l);
        return 0;
    }

    if (str_imatch(p, "PARAMETERS")) {
        lexer_t l;
        lexer_init_ext(&l, skip_ws(p + 10), cmd_get_memvar_store());
        prog_parameters(&l);
        return 0;
    }

    if (str_imatch(p, "PRIVATE")) {
        lexer_t l;
        lexer_init_ext(&l, skip_ws(p + 7), cmd_get_memvar_store());
        prog_private(&l);
        return 0;
    }

    if (str_imatch(p, "PUBLIC")) {
        lexer_t l;
        lexer_init_ext(&l, skip_ws(p + 6), cmd_get_memvar_store());
        prog_public(&l);
        return 0;
    }

    if (str_imatch(p, "LOCAL")) {
        lexer_t l;
        lexer_init_ext(&l, skip_ws(p + 5), cmd_get_memvar_store());
        prog_local(&l);
        return 0;
    }

    if (str_imatch(p, "BEGIN") && str_imatch(skip_ws(p + 5), "SEQUENCE")) {
        prog_begin_sequence();
        return 0;
    }

    if (str_imatch(p, "RECOVER")) {
        prog_recover();
        return 0;
    }

    if (str_imatch(p, "END") && str_imatch(skip_ws(p + 3), "SEQUENCE")) {
        prog_end_sequence();
        return 0;
    }

    if (str_imatch(p, "BREAK")) {
        prog_break();
        return 0;
    }

    if (str_imatch(p, "CANCEL")) {
        prog_cancel();
        return 0;
    }

    /* TEXT...ENDTEXT — output lines verbatim until ENDTEXT */
    if (str_imatch(p, "TEXT")) {
        state.pc++;
        while (state.running && state.current_prog &&
               state.pc < state.current_prog->nlines) {
            char *raw = state.current_prog->lines[state.pc];
            char *tp = skip_ws(raw);
            if (str_imatch(tp, "ENDTEXT")) {
                state.pc++;
                break;
            }
            printf("%s\n", raw);
            state.pc++;
        }
        return 0;
    }

    /* ON ERROR DO <proc> / ON ERROR */
    if (str_imatch(p, "ON") && str_imatch(skip_ws(p + 2), "ERROR")) {
        char *rest = skip_ws(skip_ws(p + 2) + 5);
        if (str_imatch(rest, "DO")) {
            rest = skip_ws(rest + 2);
            char procname[64];
            int i = 0;
            while (is_ident_char(*rest) && i < 63)
                procname[i++] = *rest++;
            procname[i] = '\0';
            prog_on_error(procname);
        } else {
            prog_on_error(NULL);
        }
        state.pc++;
        return 0;
    }

    if (str_imatch(p, "RETRY")) {
        prog_retry();
        state.pc++;
        return 0;
    }

    if (str_imatch(p, "SUSPEND")) {
        state.pc++;  /* advance past SUSPEND before pausing */
        prog_suspend();
        return 0;
    }

    if (str_imatch(p, "CASE")) {
        prog_case(skip_ws(p + 4));
        return 0;
    }

    if (str_imatch(p, "OTHERWISE")) {
        prog_otherwise();
        return 0;
    }

    if (str_imatch(p, "ENDCASE")) {
        prog_endcase();
        return 0;
    }

    /* NOTE comment */
    if (str_imatch(p, "NOTE")) {
        state.pc++;
        return 0;
    }

    /* Full-line comment */
    if (*p == '*') {
        state.pc++;
        return 0;
    }

    /* Regular command - delegate to cmd_execute */
    result = cmd_execute(NULL, line);
    state.pc++;
    return result;
}
