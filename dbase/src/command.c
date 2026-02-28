#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <dirent.h>
#include "command.h"
#include "dbf.h"
#include "field.h"
#include "expr.h"
#include "func.h"
#include "date.h"
#include "util.h"
#include "memvar.h"
#include "set.h"
#include "program.h"
#include "screen.h"
#include "index.h"
#include "report.h"
#include "label.h"
#include "lex.h"
#include "area.h"
#include "ast.h"
#include "browse.h"
#include "menu.h"
#include "csv_import.h"

/* Persistent expression context */
static expr_ctx_t expr_ctx;

/* Stage 3 state */
static memvar_store_t memvar_store;
static set_options_t set_opts;
static int stage3_initialized;

/* Forward declarations */
/* area_lookup_dbf from area.h is used as callback */
static void common_list_display(dbf_t *db, lexer_t *l, int is_display);
static void cmd_list(dbf_t *db, lexer_t *l);
static void cmd_display(dbf_t *db, lexer_t *l);
static void cmd_display_structure(dbf_t *db);
static void cmd_display_status(void);
static void cmd_replace(dbf_t *db, lexer_t *l);
static void cmd_report_form(dbf_t *db, lexer_t *l);
static void cmd_count(dbf_t *db, lexer_t *l);
static void cmd_sum(dbf_t *db, lexer_t *l);
static void cmd_average(dbf_t *db, lexer_t *l);
static void cmd_delete(dbf_t *db, lexer_t *l);
static void cmd_recall(dbf_t *db, lexer_t *l);
static void cmd_locate(dbf_t *db, lexer_t *l);
static void cmd_continue(dbf_t *db);
static void cmd_store(dbf_t *db, lexer_t *l);

static void report_expr_error(void);
static int skip_deleted(const char *rec_buf);
static int check_filter(dbf_t *db);

static work_area_t *cur_wa(void) { return area_get_current(); }
static dbf_t *cur_db(void) { return &area_get_current()->db; }

/* ---- Helper: initialize expr_ctx ---- */
static int screen_initialized;
static void ctx_setup(void) {
    if (!stage3_initialized) {
        area_init_all();
        memvar_init(&memvar_store);
        set_init(&set_opts);
        stage3_initialized = 1;
    }
    if (!screen_initialized) {
        screen_init();
        menu_init();
        screen_initialized = 1;
    }
    expr_ctx.db = &area_get_current()->db;
    expr_ctx.vars = &memvar_store;
    expr_ctx.opts = (struct set_options *)&set_opts;
    expr_ctx.area_lookup = area_lookup_dbf;
}

/* ---- Command lex helpers ---- */
static int cmd_kw(const lexer_t *l, const char *kw) {
    return l->current.type == TOK_IDENT && is_keyword(l->current.text, kw);
}

static int cmd_peek_kw(const lexer_t *l, const char *kw) {
    lexer_t tmp = *l;
    lex_next(&tmp);
    return cmd_kw(&tmp, kw);
}

static const char *cmd_after(const lexer_t *l) {
    return skip_ws(l->p);
}

static int cmd_kw_at(const char *text, const char *kw, const char **after) {
    lexer_t t;
    lexer_init_ext(&t, text, expr_ctx.vars);
    if (cmd_kw(&t, kw)) {
        if (after) *after = cmd_after(&t);
        return 1;
    }
    return 0;
}

/* ---- Index helpers ---- */
static index_t *controlling_index(void) {
    work_area_t *wa = cur_wa();
    if (wa->order > 0 && wa->order <= wa->num_indexes)
        return &wa->indexes[wa->order - 1];
    return NULL;
}

static void close_all_indexes(work_area_t *wa) {
    int i;
    for (i = 0; i < wa->num_indexes; i++) {
        if (wa->indexes[i].active)
            index_close(&wa->indexes[i]);
    }
    wa->num_indexes = 0;
    wa->order = 0;
}

/* ---- Index maintenance helpers ---- */

/* Evaluate key expression for current record, store in keybuf (space-padded to key_len) */
static int eval_index_key(index_t *idx, char *keybuf) {
    value_t val;
    char tmp[MAX_INDEX_KEY + 1];

    if (idx->key_ast) {
        if (ast_eval(idx->key_ast, &expr_ctx, &val) != 0)
            return -1;
    } else {
        if (expr_eval_str(&expr_ctx, idx->key_expr, &val) != 0)
            return -1;
    }
    index_format_key_value(idx->key_type, &val, tmp, sizeof(tmp));
    /* Pad to key_len with spaces */
    {
        int len = strlen(tmp);
        if (len > idx->key_len) len = idx->key_len;
        memcpy(keybuf, tmp, len);
        if (len < idx->key_len)
            memset(keybuf + len, ' ', idx->key_len - len);
        keybuf[idx->key_len] = '\0';
    }
    return 0;
}

/* Insert current record into all active indexes for current work area */
static void indexes_insert_current(dbf_t *db) {
    work_area_t *wa = cur_wa();
    int i;
    ctx_setup();
    for (i = 0; i < wa->num_indexes; i++) {
        index_t *idx = &wa->indexes[i];
        if (idx->active) {
            char keybuf[MAX_INDEX_KEY + 1];
            if (eval_index_key(idx, keybuf) == 0) {
                index_insert(idx, keybuf, db->current_record);
                index_flush(idx);
            }
        }
    }
}

/* Capture current keys for all active indexes (before REPLACE modifies fields) */
static void indexes_capture_keys(dbf_t *db, char keys[][MAX_INDEX_KEY + 1]) {
    work_area_t *wa = cur_wa();
    int i;
    (void)db;
    ctx_setup();
    for (i = 0; i < wa->num_indexes; i++) {
        index_t *idx = &wa->indexes[i];
        keys[i][0] = '\0';
        if (idx->active) {
            eval_index_key(idx, keys[i]);
        }
    }
}

/* Update all active indexes after REPLACE (remove old key, insert new key).
   Two-phase: validate all UNIQUE constraints first, then mutate.
   Returns 0 on success, -1 if a UNIQUE constraint was violated. */
static int indexes_update_current(dbf_t *db, char old_keys[][MAX_INDEX_KEY + 1]) {
    work_area_t *wa = cur_wa();
    char new_keys[MAX_INDEXES][MAX_INDEX_KEY + 1];
    int changed[MAX_INDEXES];
    int i;
    ctx_setup();

    /* Phase 1: evaluate new keys and check UNIQUE constraints */
    for (i = 0; i < wa->num_indexes; i++) {
        changed[i] = 0;
        index_t *idx = &wa->indexes[i];
        if (idx->active && eval_index_key(idx, new_keys[i]) == 0) {
            if (memcmp(old_keys[i], new_keys[i], idx->key_len) != 0) {
                changed[i] = 1;
                if (idx->unique &&
                    index_key_exists(idx, new_keys[i], db->current_record)) {
                    prog_error_fmt(ERR_UNIQUE, "Uniqueness violation on index %s", idx->filename);
                    return -1;
                }
            }
        }
    }

    /* Phase 2: all checks passed — apply mutations */
    for (i = 0; i < wa->num_indexes; i++) {
        if (changed[i]) {
            index_t *idx = &wa->indexes[i];
            index_remove(idx, old_keys[i], db->current_record);
            index_insert(idx, new_keys[i], db->current_record);
            index_flush(idx);
        }
    }
    return 0;
}

/* Rebuild all active indexes (after PACK changes record numbers) */
static void indexes_rebuild_all(dbf_t *db) {
    work_area_t *wa = cur_wa();
    int i;
    ctx_setup();
    for (i = 0; i < wa->num_indexes; i++) {
        index_t *idx = &wa->indexes[i];
        if (idx->active) {
            char key_expr[256], filename[64];
            str_copy(key_expr, idx->key_expr, sizeof(key_expr));
            str_copy(filename, idx->filename, sizeof(filename));
            index_build(idx, db, &expr_ctx, key_expr, filename,
                        idx->for_expr, idx->descending);
            index_write(idx);
        }
    }
}

/* Clear all active indexes (after ZAP) */
static void indexes_clear_all(void) {
    work_area_t *wa = cur_wa();
    int i;
    for (i = 0; i < wa->num_indexes; i++) {
        index_t *idx = &wa->indexes[i];
        if (idx->active) {
            index_clear(idx);
            index_write(idx);
        }
    }
}

/* ---- SET RELATION: follow child area ---- */
static void follow_relations(void) {
    work_area_t *wa = cur_wa();
    if (wa->relation_expr[0] && wa->relation_target >= 0) {
        int saved_area = area_get_current_idx();
        value_t key;
        /* Evaluate relation expression in parent context */
        ctx_setup();
        if (expr_eval_str(&expr_ctx, wa->relation_expr, &key) == 0) {
            char buf[256];
            val_to_string(&key, buf, sizeof(buf));
            /* Switch to child, seek */
            area_set_current_idx(wa->relation_target);
            {
                work_area_t *child = cur_wa();
                index_t *idx = NULL;
                if (child->order > 0 && child->order <= child->num_indexes)
                    idx = &child->indexes[child->order - 1];
                if (idx && idx->active) {
                    index_format_key_value(idx->key_type, &key, buf, sizeof(buf));
                    index_seek(idx, buf, 0);
                    {
                        uint32_t rec = index_current_recno(idx);
                        if (rec > 0)
                            dbf_read_record(&child->db, rec);
                        else
                            child->db.current_record = child->db.record_count + 1;
                    }
                } else {
                    /* No index: record number search */
                    uint32_t rec = (uint32_t)atoi(buf); 
                    if (rec >= 1 && rec <= child->db.record_count)
                        dbf_read_record(&child->db, rec);
                    else
                        child->db.current_record = child->db.record_count + 1;
                }
            }
        }
        area_set_current_idx(saved_area);
    }
}

/* ---- Helper: check FOR clause ---- */
static const char *find_for_clause(const char *p) {
    const char *s = p;
    while (*s) {
        s = skip_ws(s);
        if (str_imatch(s, "FOR")) {
            return skip_ws(s + 3);
        }
        s++;
    }
    return NULL;
}

/* ---- Helper: parse field list before FOR clause ---- */
static int parse_field_list(dbf_t *db, const char *arg, int *indices, int max_fields, const char **rest) {
    const char *p = skip_ws(arg);
    int count = 0;
    char name[DBF_MAX_FIELD_NAME];

    *rest = p;

    while (*p && count < max_fields) {
        int i, idx;

        if (str_imatch(p, "FOR")) break;
        /* Also stop at scope keywords that might appear in COPY TO context */
        if (str_imatch(p, "ALL") || str_imatch(p, "REST") ||
            str_imatch(p, "NEXT") || str_imatch(p, "RECORD")) break;
        if (str_imatch(p, "DELIMITED")) break;

        i = 0;
        while (*p && *p != ',' && *p != ' ' && *p != '\t' && i < DBF_MAX_FIELD_NAME - 1)
            name[i++] = *p++;
        name[i] = '\0';

        if (name[0] == '\0') break;

        idx = dbf_find_field(db, name);
        if (idx < 0) {
            prog_error_fmt(ERR_SYNTAX, "Field not found: %s", name);
            return -1;
        }
        indices[count++] = idx;

        p = skip_ws(p);
        if (*p == ',') { p++; p = skip_ws(p); }
    }

    *rest = p;
    return count;
}

/* ---- Helper: display one field value ---- */
#define PRINT_TO(f, ...) do { if (f) fprintf(f, __VA_ARGS__); else printf(__VA_ARGS__); } while(0)

static void print_field_value(dbf_t *db, int f, char *raw, char *display, FILE *to_file, int for_list, int width) {
    dbf_get_field_raw(db, f, raw, 256);
    switch (db->fields[f].type) {
    case 'C':
        field_display_char(display, raw, db->fields[f].length);
        if (for_list) {
            trim_right(display);
            PRINT_TO(to_file, " %-*s", width, display);
        } else {
            trim_right(display);
            PRINT_TO(to_file, "%10s: %s\n", db->fields[f].name, display);
        }
        break;
    case 'F':
    case 'N':
        field_display_numeric(display, raw, db->fields[f].length);
        if (for_list)
            PRINT_TO(to_file, " %*s", width, display);
        else
            PRINT_TO(to_file, "%10s: %s\n", db->fields[f].name, display);
        break;
    case 'D':
        field_display_date(display, raw);
        if (for_list)
            PRINT_TO(to_file, " %-8s", display);
        else
            PRINT_TO(to_file, "%10s: %s\n", db->fields[f].name, display);
        break;
    case 'L':
        field_display_logical(display, raw);
        if (for_list)
            PRINT_TO(to_file, " %-3s", display);
        else
            PRINT_TO(to_file, "%10s: %s\n", db->fields[f].name, display);
        break;
    case 'M': {
        int blk = atoi(raw);
        if (for_list) {
            PRINT_TO(to_file, " %-*s", width, blk > 0 ? "memo" : "");
        } else {
            if (blk > 0) {
                char memo[256];
                dbf_memo_read(db, blk, memo, sizeof(memo));
                PRINT_TO(to_file, "%10s: %s\n", db->fields[f].name, memo);
            } else {
                PRINT_TO(to_file, "%10s:\n", db->fields[f].name);
            }
        }
        break;
    }
    default:
        field_display_char(display, raw, db->fields[f].length);
        if (for_list) {
            trim_right(display);
            PRINT_TO(to_file, " %-*s", width, display);
        } else {
            trim_right(display);
            PRINT_TO(to_file, "%10s: %s\n", db->fields[f].name, display);
        }
        break;
    }
}

static int field_display_width(dbf_t *db, int f) {
    int w = db->fields[f].length;
    int nlen = strlen(db->fields[f].name);
    if (w < nlen) w = nlen;
    if (db->fields[f].type == 'D') w = 8;
    if (db->fields[f].type == 'L') w = 3;
    if (db->fields[f].type == 'M') { w = 4; if (nlen > w) w = nlen; }
    return w;
}

/* ---- Helper: extract filename stem (without .DBF extension) ---- */
static void filename_stem(const char *filename, char *stem, int size) {
    const char *base;
    int len, slen;
    /* Use just the basename (strip directory) */
    base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    len = strlen(base);
    /* Strip .DBF extension before copying to potentially short buffer */
    if (len >= 4 && str_icmp(base + len - 4, ".DBF") == 0)
        len -= 4;
    slen = (len < size - 1) ? len : size - 1;
    memcpy(stem, base, slen);
    stem[slen] = '\0';
}

/* Uppercase only the filename portion (after last slash), not directory components */
static void upper_basename(char *path) {
    char *base = strrchr(path, '/');
    str_upper(base ? base + 1 : path);
}

/* ---- Helper: ensure filename has .DBF extension ---- */
static void ensure_dbf_ext(char *filename, int size) {
    upper_basename(filename);
    trim_right(filename);
    if (strlen(filename) < 5 || str_icmp(filename + strlen(filename) - 4, ".DBF") != 0) {
        if ((int)strlen(filename) + 4 < size)
            strcat(filename, ".DBF");
    }
}

/* ---- Helper: ensure filename has .NDX extension ---- */
static void ensure_ndx_ext(char *filename, int size) {
    upper_basename(filename);
    trim_right(filename);
    if (strlen(filename) < 5 || str_icmp(filename + strlen(filename) - 4, ".NDX") != 0) {
        if ((int)strlen(filename) + 4 < size)
            strcat(filename, ".NDX");
    }
}

/* ---- Helper: parse filename from arg ---- */
static const char *parse_filename(const char *arg, char *filename, int size) {
    const char *p = skip_ws(arg);
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < size - 1)
        filename[i++] = *p++;
    filename[i] = '\0';
    path_normalize(filename);
    ensure_dbf_ext(filename, size);
    return p;
}

/* ---- CREATE ---- */
static void cmd_create(dbf_t *db, const char *arg) {
    char filename[64];
    dbf_field_t fields[DBF_MAX_FIELDS];
    int nfields = 0;
    char line[256];
    char type_line[16];
    char width_line[16];
    char dec_line[16];

    (void)db;
    arg = skip_ws(arg);
    if (*arg == '\0') {
        printf("Syntax: CREATE <filename>\n");
        return;
    }

    str_copy(filename, arg, sizeof(filename));
    path_normalize(filename);
    path_normalize(filename);
    ensure_dbf_ext(filename, sizeof(filename));

    printf("Enter field definitions (blank name to finish):\n");

    while (nfields < DBF_MAX_FIELDS) {
        printf("Field name: ");
        if (read_line(line, sizeof(line)) < 0) break;
        trim_right(line);
        if (line[0] == '\0') break;

        str_upper(line);
        str_copy(fields[nfields].name, line, DBF_MAX_FIELD_NAME);

        printf("Type (C/N/D/L/M): ");
        if (read_line(type_line, sizeof(type_line)) < 0) break;
        trim_right(type_line);
        str_upper(type_line);

        fields[nfields].type = type_line[0];
        fields[nfields].decimals = 0;

        if (type_line[0] == 'M') {
            fields[nfields].length = 10;
        } else if (type_line[0] == 'D') {
            fields[nfields].length = 8;
        } else if (type_line[0] == 'L') {
            fields[nfields].length = 1;
        } else {
            int maxw = (type_line[0] == 'N') ? 19 : 254;
            printf("Width (1-%d): ", maxw);
            if (read_line(width_line, sizeof(width_line)) < 0) break;
            fields[nfields].length = atoi(width_line);
            if (fields[nfields].length < 1) fields[nfields].length = 1;
            if (fields[nfields].length > maxw) fields[nfields].length = maxw;

            if (type_line[0] == 'N') {
                printf("Decimals: ");
                if (read_line(dec_line, sizeof(dec_line)) < 0) break;
                fields[nfields].decimals = atoi(dec_line);
                if (fields[nfields].decimals >= fields[nfields].length)
                    fields[nfields].decimals = fields[nfields].length - 2;
                if (fields[nfields].decimals < 0) fields[nfields].decimals = 0;
            }
        }

        fields[nfields].offset = 0;
        nfields++;
    }

    if (nfields == 0) {
        printf("No fields defined.\n");
        return;
    }

    if (dbf_create(filename, fields, nfields) < 0) {
        prog_error_fmt(ERR_CANNOT_CREATE, "Error creating %s", filename);
        return;
    }

    /* Create .DBT memo file if any memo fields */
    {
        int mi;
        for (mi = 0; mi < nfields; mi++) {
            if (fields[mi].type == 'M') {
                dbf_memo_create(filename);
                break;
            }
        }
    }

    printf("Database %s created with %d field(s).\n", filename, nfields);
}

/* ---- SELECT ---- */
static void cmd_select(const char *arg) {
    const char *p = skip_ws(arg);
    int idx;

    if (*p == '\0') {
        printf("Current work area: %d\n", area_get_current_idx() + 1);
        return;
    }

    idx = area_resolve_alias(p);
    if (idx >= 0) {
        area_set_current_idx(idx);
        return;
    }

    prog_error(ERR_ALIAS_NOT_FOUND, "Invalid work area");
}

/* ---- Helper: report file-not-found error ---- */
static void file_not_found(const char *filename) {
    char buf[128];
    snprintf(buf, sizeof(buf), "File not found: %s", filename);
    prog_error(ERR_FILE_NOT_FOUND, buf);
}

/* ---- Helper: report expression error via prog_error ---- */
static void report_expr_error(void) {
    if (expr_ctx.error) {
        prog_error(expr_error_code(expr_ctx.error), expr_ctx.error);
    }
}

/* ---- Helper: check if record should be skipped (SET DELETED) ---- */
static int skip_deleted(const char *rec_buf) {
    return (rec_buf[0] == '*' && set_opts.deleted);
}

/* ---- Helper: check if current record passes SET FILTER ---- */
/* Returns 1 if record passes (or no filter set), 0 if filtered out */
static int check_filter(dbf_t *db) {
    value_t cond;
    (void)db;
    if (cur_wa()->filter_cond[0] == '\0')
        return 1;
    if (cur_wa()->filter_ast) {
        if (ast_eval(cur_wa()->filter_ast, &expr_ctx, &cond) != 0)
            return 1;  /* on error, pass through */
    } else {
        if (ast_eval_dynamic(cur_wa()->filter_cond, &expr_ctx, &cond) != 0)
            return 1;  /* on error, pass through */
    }
    return (cond.type == VAL_LOGIC && cond.logic);
}

/* ---- USE ---- */
static void cmd_use(dbf_t *db, const char *arg) {
    char filename[64];
    const char *p;
    int i;
    work_area_t *wa = cur_wa();

    arg = skip_ws(arg);

    if (*arg == '\0') {
        if (dbf_is_open(db)) {
            dbf_close(db);
            close_all_indexes(wa);
            wa->alias[0] = '\0';
            wa->locate_cond[0] = '\0';
            wa->locate_last_rec = 0;
            wa->filter_cond[0] = '\0';
            if (wa->filter_ast) { ast_free(wa->filter_ast); wa->filter_ast = NULL; }
            if (set_opts.talk) printf("Database closed.\n");
        }
        return;
    }

    if (dbf_is_open(db)) {
        dbf_close(db);
        close_all_indexes(wa);
        wa->alias[0] = '\0';
    }

    /* Parse filename (up to whitespace) */
    p = arg;
    i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < 63)
        filename[i++] = *p++;
    filename[i] = '\0';
    if (filename[0] == '\0') {
        printf("Syntax: USE [filename] [INDEX idx] [ALIAS alias]\n");
        return;
    }

    ensure_dbf_ext(filename, sizeof(filename));

    {
        char resolved[320];
        if (cmd_resolve_file(filename, resolved, sizeof(resolved)) == 0)
            str_copy(filename, resolved, sizeof(filename));
    }

    if (dbf_open(db, filename) < 0) {
        file_not_found(filename);
        return;
    }

    /* Set alias to filename stem */
    filename_stem(filename, wa->alias, sizeof(wa->alias));

    /* Parse optional INDEX clause */
    p = skip_ws(p);
    if (str_imatch(p, "INDEX")) {
        p = skip_ws(p + 5);
        while (*p && !str_imatch(p, "ALIAS") && wa->num_indexes < MAX_INDEXES) {
            char ndxfile[64];
            i = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != ',' && i < 63)
                ndxfile[i++] = *p++;
            ndxfile[i] = '\0';

            if (ndxfile[0]) {
                ensure_ndx_ext(ndxfile, sizeof(ndxfile));
                {
                    int slot = wa->num_indexes;
                    if (index_read(&wa->indexes[slot], ndxfile) == 0) {
                        index_compile_key_ast(&wa->indexes[slot], &memvar_store);
                        wa->num_indexes++;
                    } else {
                        prog_error_fmt(ERR_CANNOT_OPEN, "Cannot open index: %s", ndxfile);
                    }
                }
            }

            p = skip_ws(p);
            if (*p == ',') { p++; p = skip_ws(p); }
            else break;
        }

        if (wa->num_indexes > 0)
            wa->order = 1;
    }

    /* Parse optional ALIAS clause */
    p = skip_ws(p);
    if (str_imatch(p, "ALIAS")) {
        p = skip_ws(p + 5);
        i = 0;
        while (*p && *p != ' ' && *p != '\t' && i < DBF_MAX_FIELD_NAME - 1)
            wa->alias[i++] = *p++;
        wa->alias[i] = '\0';
        str_upper(wa->alias);
    }

    /* Reset navigation state */
    expr_ctx.eof_flag = 0;
    expr_ctx.bof_flag = 0;
    expr_ctx.found = 0;
    wa->locate_cond[0] = '\0';
    wa->locate_last_rec = 0;

    if (set_opts.talk)
        printf("Database %s opened with %d record(s).\n",
               db->filename, (int)db->record_count);
}

/* ---- APPEND BLANK ---- */
static void cmd_append_blank(dbf_t *db) {
    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    if (dbf_append_blank(db) < 0) {
        prog_error(ERR_FILE_IO, "Error appending record");
        return;
    }

    indexes_insert_current(db);

    expr_ctx.eof_flag = 0;
    expr_ctx.bof_flag = 0;
    if (set_opts.talk)
        printf("Record %d added.\n", (int)db->record_count);
}

/* ---- GO / GOTO ---- */
static void cmd_go(dbf_t *db, const char *arg) {
    const char *p;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    p = skip_ws(arg);

    if (str_imatch(p, "TOP")) {
        if (db->record_count == 0) {
            expr_ctx.eof_flag = 1;
            expr_ctx.bof_flag = 1;
            prog_error(ERR_RECORD_RANGE, "No records");
            return;
        }
        {
            index_t *idx = controlling_index();
            if (idx) {
                uint32_t rec;
                index_top(idx);
                rec = index_current_recno(idx);
                if (rec > 0) dbf_read_record(db, rec);
                /* Advance past filtered records */
                while (rec > 0 && (!check_filter(db) || skip_deleted(db->record_buf))) {
                    if (index_next(idx) < 0) {
                        expr_ctx.eof_flag = 1;
                        db->current_record = db->record_count + 1;
                        return;
                    }
                    rec = index_current_recno(idx);
                    if (rec > 0) dbf_read_record(db, rec);
                }
            } else {
                uint32_t r;
                dbf_read_record(db, 1);
                /* Advance past filtered records */
                for (r = 1; r <= db->record_count; r++) {
                    dbf_read_record(db, r);
                    if (!skip_deleted(db->record_buf) && check_filter(db))
                        break;
                }
                if (r > db->record_count) {
                    expr_ctx.eof_flag = 1;
                    db->current_record = db->record_count + 1;
                    return;
                }
            }
        }
        expr_ctx.eof_flag = 0;
        expr_ctx.bof_flag = 0;
        return;
    }

    if (str_imatch(p, "BOTTOM")) {
        if (db->record_count == 0) {
            expr_ctx.eof_flag = 1;
            expr_ctx.bof_flag = 1;
            prog_error(ERR_RECORD_RANGE, "No records");
            return;
        }
        {
            index_t *idx = controlling_index();
            if (idx) {
                uint32_t rec;
                index_bottom(idx);
                rec = index_current_recno(idx);
                if (rec > 0) dbf_read_record(db, rec);
                /* Back up past filtered records */
                while (rec > 0 && (!check_filter(db) || skip_deleted(db->record_buf))) {
                    if (index_prev(idx) < 0) {
                        expr_ctx.bof_flag = 1;
                        return;
                    }
                    rec = index_current_recno(idx);
                    if (rec > 0) dbf_read_record(db, rec);
                }
            } else {
                uint32_t r;
                /* Scan backward for last matching record */
                for (r = db->record_count; r >= 1; r--) {
                    dbf_read_record(db, r);
                    if (!skip_deleted(db->record_buf) && check_filter(db))
                        break;
                }
                if (r < 1) {
                    expr_ctx.bof_flag = 1;
                    return;
                }
            }
        }
        expr_ctx.eof_flag = 0;
        expr_ctx.bof_flag = 0;
        return;
    }

    /* GO n */
    {
        int n = atoi(p);
        if (n < 1 || (uint32_t)n > db->record_count) {
            prog_error(ERR_RECORD_RANGE, "Record out of range");
            return;
        }
        dbf_read_record(db, (uint32_t)n);
        expr_ctx.eof_flag = 0;
        expr_ctx.bof_flag = 0;
    }
}

/* ---- SKIP ---- */
static void cmd_skip(dbf_t *db, const char *arg) {
    const char *p;
    int n;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    p = skip_ws(arg);
    if (*p == '\0')
        n = 1;
    else
        n = atoi(p);

    /* Index-ordered skip */
    {
        index_t *idx = controlling_index();
    if (idx) {
        int i, found;
        uint32_t rec;

        if (n > 0) {
            for (i = 0; i < n; i++) {
                found = 0;
                while (!found) {
                    if (index_next(idx) < 0) {
                        expr_ctx.eof_flag = 1;
                        expr_ctx.bof_flag = 0;
                        db->current_record = db->record_count + 1;
                        return;
                    }
                    rec = index_current_recno(idx);
                    if (rec > 0) dbf_read_record(db, rec);
                    if (!skip_deleted(db->record_buf) && check_filter(db))
                        found = 1;
                }
            }
        } else if (n < 0) {
            for (i = 0; i < -n; i++) {
                found = 0;
                while (!found) {
                    if (index_prev(idx) < 0) {
                        index_top(idx);
                        rec = index_current_recno(idx);
                        if (rec > 0) dbf_read_record(db, rec);
                        expr_ctx.bof_flag = 1;
                        expr_ctx.eof_flag = 0;
                        return;
                    }
                    rec = index_current_recno(idx);
                    if (rec > 0) dbf_read_record(db, rec);
                    if (!skip_deleted(db->record_buf) && check_filter(db))
                        found = 1;
                }
            }
        }

        rec = index_current_recno(idx);
        if (rec > 0) {
            dbf_read_record(db, rec);
            expr_ctx.eof_flag = 0;
            expr_ctx.bof_flag = 0;
        }
        return;
    }
    }

    /* Physical order skip */
    {
        int32_t target;
        int dir = (n > 0) ? 1 : -1;
        int remaining = (n > 0) ? n : -n;

        if (db->current_record == 0) {
            if (db->record_count == 0) {
                expr_ctx.eof_flag = 1;
                return;
            }
            dbf_read_record(db, 1);
            expr_ctx.eof_flag = 0;
            expr_ctx.bof_flag = 0;
            if (n > 0 && !skip_deleted(db->record_buf) && check_filter(db)) {
                remaining--;
            }
            if (remaining == 0) return;
        }

        target = (int32_t)db->current_record;
        while (remaining > 0) {
            target += dir;
            if (target < 1) {
                dbf_read_record(db, 1);
                expr_ctx.bof_flag = 1;
                expr_ctx.eof_flag = 0;
                return;
            }
            if ((uint32_t)target > db->record_count) {
                expr_ctx.eof_flag = 1;
                expr_ctx.bof_flag = 0;
                db->current_record = db->record_count + 1;
                return;
            }
            dbf_read_record(db, (uint32_t)target);
            if (!skip_deleted(db->record_buf) && check_filter(db))
                remaining--;
        }
        expr_ctx.eof_flag = 0;
        expr_ctx.bof_flag = 0;
    }
}

/* ---- Scope infrastructure ---- */

void clause_init(clause_t *c) {
    memset(c, 0, sizeof(*c));
    c->scope.type = SCOPE_DEFAULT;
    c->has_scope = 0;
}

void clause_compile(clause_t *c, memvar_store_t *store) {
    const char *error;
    if (c->for_cond[0] && !c->for_ast && !strchr(c->for_cond, '&')) {
        c->for_ast = ast_compile(c->for_cond, store, &error);
    }
    if (c->while_cond[0] && !c->while_ast && !strchr(c->while_cond, '&')) {
        c->while_ast = ast_compile(c->while_cond, store, &error);
    }
}

void clause_free_ast(clause_t *c) {
    if (c->for_ast) { ast_free(c->for_ast); c->for_ast = NULL; }
    if (c->while_ast) { ast_free(c->while_ast); c->while_ast = NULL; }
}

static int consume_filename(lexer_t *l, char *out, int size) {
    const char *p = l->token_start;
    const char *start;
    int len = 0;
    char quote;

    p = skip_ws(p);
    if (*p == '\0') return -1;

    if (*p == '"' || *p == '\'') {
        quote = *p++;
        start = p;
        while (*p && *p != quote) p++;
        len = (int)(p - start);
        if (*p == quote) p++;
    } else {
        start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        len = (int)(p - start);
    }

    if (len >= size) len = size - 1;
    memcpy(out, start, len);
    out[len] = '\0';

    lexer_init_ext(l, p, l->store);
    return 0;
}

/* Parse common dBase clauses (FOR, WHILE, TO PRINT, TO FILE, OFF, etc.)
   Returns 0 on success. Consumes tokens until end of line or unknown keyword. */
int parse_clauses(lexer_t *l, clause_t *c) {
    while (l->current.type != TOK_EOF) {
        if (cmd_kw(l, "FOR")) {
            lex_next(l);
            const char *start = l->token_start;
            value_t dummy;
            expr_eval(&expr_ctx, &start, &dummy);
            int len = (int)(start - l->token_start);
            if (len > (int)sizeof(c->for_cond) - 1) len = sizeof(c->for_cond) - 1;
            memcpy(c->for_cond, l->token_start, len);
            c->for_cond[len] = '\0';
            lexer_init_ext(l, start, l->store);
        } else if (cmd_kw(l, "WHILE")) {
            lex_next(l);
            const char *start = l->token_start;
            value_t dummy;
            expr_eval(&expr_ctx, &start, &dummy);
            int len = (int)(start - l->token_start);
            if (len > (int)sizeof(c->while_cond) - 1) len = sizeof(c->while_cond) - 1;
            memcpy(c->while_cond, l->token_start, len);
            c->while_cond[len] = '\0';
            lexer_init_ext(l, start, l->store);
        } else if (cmd_kw(l, "TO")) {
            lex_next(l);
            if (cmd_kw(l, "PRINT")) {
                c->to_print = 1;
                lex_next(l);
            } else if (cmd_kw(l, "FILE")) {
                lex_next(l);
                if (consume_filename(l, c->to_file, sizeof(c->to_file)) < 0)
                    return -1;
            } else if (l->current.type == TOK_IDENT) {
                /* TO <variable> for COUNT/SUM/AVERAGE */
                str_copy(c->to_var, l->current.text, sizeof(c->to_var));
                lex_next(l);
            } else {
                return -1; /* Unknown TO clause */
            }
        } else if (cmd_kw(l, "OFF")) {
            c->off = 1;
            lex_next(l);
        } else if (cmd_kw(l, "PLAIN")) {
            c->plain = 1;
            lex_next(l);
        } else if (cmd_kw(l, "SUMMARY")) {
            c->summary = 1;
            lex_next(l);
        } else if (cmd_kw(l, "NOEJECT")) {
            c->noeject = 1;
            lex_next(l);
        } else if (cmd_kw(l, "HEADING")) {
            lex_next(l);
            if (l->current.type == TOK_STRING) {
                str_copy(c->heading, l->current.text, sizeof(c->heading));
                lex_next(l);
            }
        } else if (cmd_kw(l, "ALL")) {
            c->scope.type = SCOPE_ALL;
            c->has_scope = 1;
            lex_next(l);
        } else if (cmd_kw(l, "NEXT")) {
            lex_next(l);
            if (l->current.type == TOK_NUMBER) {
                c->scope.type = SCOPE_NEXT;
                c->scope.count = (uint32_t)l->current.num_val;
                c->has_scope = 1;
                lex_next(l);
            }
        } else if (cmd_kw(l, "RECORD")) {
            lex_next(l);
            if (l->current.type == TOK_NUMBER) {
                c->scope.type = SCOPE_RECORD;
                c->scope.count = (uint32_t)l->current.num_val;
                c->has_scope = 1;
                lex_next(l);
            }
        } else if (cmd_kw(l, "REST")) {
            c->scope.type = SCOPE_REST;
            c->has_scope = 1;
            lex_next(l);
        } else {
            /* Unknown token - might be field list or syntax error */
            return 0;
        }
    }
    return 0;
}

typedef int (*record_callback_t)(dbf_t *db, uint32_t recno, void *userdata);

typedef enum {
    REC_CONTINUE = 0,
    REC_STOP = 1,
    REC_ERROR = -1
} record_result_t;

#define PROC_USE_FILTER        0x01
#define PROC_SKIP_DELETED_SET  0x02
#define PROC_SKIP_DELETED_ANY  0x04
#define PROC_REQUIRE_DELETED   0x08
#define PROC_LEAVE_ON_MATCH    0x10

/* Generic record processor for commands with scope/FOR/WHILE.
   Handles index traversal if active. Returns number of records processed. */
static int process_records(dbf_t *db, clause_t *c, int flags, record_callback_t cb, void *userdata) {
    index_t *idx = controlling_index();
    uint32_t remaining = 0xFFFFFFFF;
    int has_limit = 0;
    int count = 0;
    uint32_t last_matched = 0;

    /* Compile clause ASTs (parse once, evaluate many) */
    clause_compile(c, &memvar_store);

    /* Default scope logic:
       If no explicit scope was parsed, but a FOR was present, dBase defaults to ALL.
       Otherwise, use the command-provided default (usually provided in c->scope before parse_clauses).
    */
    if (!c->has_scope) {
        if (c->for_cond[0]) c->scope.type = SCOPE_ALL;
        else if (c->scope.type == SCOPE_ALL_DEFAULT) c->scope.type = SCOPE_ALL;
        else if (c->scope.type == SCOPE_DEFAULT) {
            /* If no scope, no FOR, and still DEFAULT, fall back to NEXT 1 */
            c->scope.type = SCOPE_NEXT;
            c->scope.count = 1;
        }
    }

    /* Initial positioning */
    if (c->scope.type == SCOPE_ALL) {
        if (idx) index_top(idx);
        else dbf_read_record(db, 1);
        expr_ctx.eof_flag = 0;
        expr_ctx.bof_flag = 0;
    } else if (c->scope.type == SCOPE_RECORD) {
        dbf_read_record(db, c->scope.count);
        remaining = 1;
        has_limit = 1;
        expr_ctx.eof_flag = 0;
    } else if (c->scope.type == SCOPE_NEXT) {
        remaining = c->scope.count;
        has_limit = 1;
    }

    while (!expr_ctx.eof_flag && (!has_limit || remaining > 0)) {
        uint32_t current_rec = idx ? index_current_recno(idx) : db->current_record;
        if (current_rec == 0 || current_rec > db->record_count) break;

        dbf_read_record(db, current_rec);

        int matched = 1;
        if (flags & PROC_SKIP_DELETED_ANY) {
            if (db->record_buf[0] == '*') matched = 0;
        } else if (flags & PROC_SKIP_DELETED_SET) {
            if (skip_deleted(db->record_buf)) matched = 0;
        }
        if (matched && (flags & PROC_REQUIRE_DELETED)) {
            if (db->record_buf[0] != '*') matched = 0;
        }
        if (matched && (flags & PROC_USE_FILTER)) {
            if (!check_filter(db)) matched = 0;
        }

        if (matched && c->for_ast) {
            value_t cond;
            if (ast_eval(c->for_ast, &expr_ctx, &cond) != 0) { report_expr_error(); count = -1; goto cleanup; }
            if (cond.type != VAL_LOGIC || !cond.logic) matched = 0;
        } else if (matched && c->for_cond[0]) {
            value_t cond;
            if (ast_eval_dynamic(c->for_cond, &expr_ctx, &cond) != 0) { report_expr_error(); count = -1; goto cleanup; }
            if (cond.type != VAL_LOGIC || !cond.logic) matched = 0;
        }

        if (matched && c->while_ast) {
            value_t cond;
            if (ast_eval(c->while_ast, &expr_ctx, &cond) != 0) { report_expr_error(); count = -1; goto cleanup; }
            if (cond.type != VAL_LOGIC || !cond.logic) break;
        } else if (matched && c->while_cond[0]) {
            value_t cond;
            if (ast_eval_dynamic(c->while_cond, &expr_ctx, &cond) != 0) { report_expr_error(); count = -1; goto cleanup; }
            if (cond.type != VAL_LOGIC || !cond.logic) break;
        }

        if (matched) {
            int cb_result;
            last_matched = current_rec;
            cb_result = cb(db, current_rec, userdata);
            if (cb_result == REC_ERROR) { count = -1; goto cleanup; }
            count++;
            if (has_limit) remaining--;
            if (cb_result == REC_STOP) break;
        }

        /* Advance */
        if (idx) {
            if (index_next(idx) < 0) expr_ctx.eof_flag = 1;
        } else {
            if (current_rec >= db->record_count) expr_ctx.eof_flag = 1;
            else dbf_read_record(db, current_rec + 1);
        }
        if (c->scope.type == SCOPE_RECORD) break;
    }

    /* If we matched anything, leave pointer on last record processed (dBase rule for DELETE/REPLACE) */
    if ((flags & PROC_LEAVE_ON_MATCH) && last_matched > 0) {
        dbf_read_record(db, last_matched);
        /* If it was SCOPE_NEXT 1 (default), we might want to preserve EOF?
           Actually dBase leaves it on the record. */
        expr_ctx.eof_flag = 0;
    }

cleanup:
    clause_free_ast(c);
    return count;
}

static scope_t parse_scope(const char **pp) {
    scope_t s;
    const char *p = skip_ws(*pp);
    s.type = SCOPE_ALL;
    s.count = 0;

    if (str_imatch(p, "ALL")) {
        s.type = SCOPE_ALL;
        *pp = skip_ws(p + 3);
    } else if (str_imatch(p, "REST")) {
        s.type = SCOPE_REST;
        *pp = skip_ws(p + 4);
    } else if (str_imatch(p, "RECORD")) {
        s.type = SCOPE_RECORD;
        p = skip_ws(p + 6);
        s.count = atoi(p);
        while (*p >= '0' && *p <= '9') p++;
        *pp = skip_ws(p);
    } else if (str_imatch(p, "NEXT")) {
        s.type = SCOPE_NEXT;
        p = skip_ws(p + 4);
        s.count = atoi(p);
        if (s.count == 0) s.count = 1;
        while (*p >= '0' && *p <= '9') p++;
        *pp = skip_ws(p);
    }

    return s;
}

int scope_bounds(dbf_t *db, const scope_t *s, uint32_t *start, uint32_t *end) {
    switch (s->type) {
    case SCOPE_DEFAULT:
        *start = (db->current_record > 0) ? db->current_record : 1;
        *end = *start;
        break;
    case SCOPE_ALL_DEFAULT:
        *start = 1;
        *end = db->record_count;
        break;
    case SCOPE_ALL:
        *start = 1;
        *end = db->record_count;
        break;
    case SCOPE_REST:
        *start = (db->current_record > 0) ? db->current_record : 1;
        *end = db->record_count;
        break;
    case SCOPE_RECORD:
        if (s->count < 1 || s->count > db->record_count) return -1;
        *start = s->count;
        *end = s->count;
        break;
    case SCOPE_NEXT:
        *start = (db->current_record > 0) ? db->current_record : 1;
        *end = *start + s->count - 1;
        if (*end > db->record_count) *end = db->record_count;
        break;
    }
    return 0;
}

static int locate_cb(dbf_t *db, uint32_t recno, void *userdata) {
    (void)db;
    cur_wa()->locate_last_rec = recno;
    expr_ctx.found = 1;
    if (set_opts.talk) printf("Record = %d\n", (int)recno);
    return REC_STOP; /* Stop after first match */
}

/* ---- LOCATE [scope] FOR ---- */
static void cmd_locate(dbf_t *db, lexer_t *l) {
    clause_t c;
    int count;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    clause_init(&c);
    c.scope.type = SCOPE_ALL_DEFAULT;
    if (parse_clauses(l, &c) < 0) return;

    if (c.for_cond[0] == '\0') {
        printf("Syntax: LOCATE [scope] FOR <condition>\n");
        return;
    }

    /* Save for CONTINUE */
    str_copy(cur_wa()->locate_cond, c.for_cond, sizeof(cur_wa()->locate_cond));
    expr_ctx.found = 0;

    count = process_records(db, &c, PROC_USE_FILTER | PROC_SKIP_DELETED_SET | PROC_LEAVE_ON_MATCH, locate_cb, NULL);
    if (count < 0) return;
    if (count == 0) {
        expr_ctx.found = 0;
        expr_ctx.eof_flag = 1;
        cur_wa()->locate_last_rec = 0;
        if (set_opts.talk) printf("End of LOCATE scope\n");
    }
}

/* ---- CONTINUE ---- */
static void cmd_continue(dbf_t *db) {
    clause_t c;
    int count;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    if (cur_wa()->locate_cond[0] == '\0') {
        printf("No LOCATE active.\n");
        return;
    }

    clause_init(&c);
    c.scope.type = SCOPE_REST;
    c.has_scope = 1;
    str_copy(c.for_cond, cur_wa()->locate_cond, sizeof(c.for_cond));

    /* CONTINUE starts from NEXT record */
    if (db->current_record < db->record_count) {
        dbf_read_record(db, db->current_record + 1);
    } else {
        expr_ctx.eof_flag = 1;
        expr_ctx.found = 0;
        if (set_opts.talk) printf("End of LOCATE scope\n");
        return;
    }

    expr_ctx.found = 0;
    count = process_records(db, &c, PROC_USE_FILTER | PROC_SKIP_DELETED_SET | PROC_LEAVE_ON_MATCH, locate_cb, NULL);
    if (count < 0) return;
    if (count == 0) {
        expr_ctx.found = 0;
        expr_ctx.eof_flag = 1;
        if (set_opts.talk) printf("End of LOCATE scope\n");
    }
}

/* ---- COUNT [scope] [FOR cond] ---- */
static int count_cb(dbf_t *db, uint32_t recno, void *userdata) {
    (void)db; (void)recno; (void)userdata;
    return REC_CONTINUE; /* Just counting matches */
}

static void cmd_count(dbf_t *db, lexer_t *l) {
    clause_t c;
    int count;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    clause_init(&c);
    c.scope.type = SCOPE_ALL_DEFAULT;
    if (parse_clauses(l, &c) < 0) return;

    count = process_records(db, &c, PROC_USE_FILTER | PROC_SKIP_DELETED_SET, count_cb, NULL);
    if (count < 0) return;
    if (set_opts.talk)
        printf("%d record(s)\n", count);
    if (c.to_var[0]) {
        value_t v = val_num((double)count);
        memvar_set(&memvar_store, c.to_var, &v);
    }
}

typedef struct {
    char expr[256];
    double total;
} agg_ctx_t;

static int sum_cb(dbf_t *db, uint32_t recno, void *userdata) {
    agg_ctx_t *ctx = (agg_ctx_t *)userdata;
    value_t val;
    (void)db; (void)recno;

    if (expr_eval_str(&expr_ctx, ctx->expr, &val) != 0) {
        report_expr_error();
        return REC_ERROR;
    }
    if (val.type != VAL_NUM) {
        printf("SUM requires numeric expression.\n");
        return REC_ERROR;
    }
    ctx->total += val.num;
    return REC_CONTINUE;
}

static void cmd_sum(dbf_t *db, lexer_t *l) {
    clause_t c;
    agg_ctx_t actx;
    int count;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    clause_init(&c);
    c.scope.type = SCOPE_ALL_DEFAULT;

    if (l->current.type == TOK_EOF) {
        printf("Syntax: SUM <expr> [scope] [FOR <cond>] [WHILE <cond>]\n");
        return;
    }

    /* Optional leading scope (ALL/NEXT/RECORD/REST) */
    if (cmd_kw(l, "ALL") || cmd_kw(l, "NEXT") || cmd_kw(l, "RECORD") || cmd_kw(l, "REST")) {
        if (parse_clauses(l, &c) < 0) return;
    }

    /* Parse expression, then trailing clauses (FOR/WHILE/scope) */
    {
        const char *expr_start = l->token_start;
        lexer_t tmp = *l;
        while (tmp.current.type != TOK_EOF) {
            if (cmd_kw(&tmp, "FOR") || cmd_kw(&tmp, "WHILE") ||
                cmd_kw(&tmp, "TO") ||
                cmd_kw(&tmp, "ALL") || cmd_kw(&tmp, "NEXT") ||
                cmd_kw(&tmp, "RECORD") || cmd_kw(&tmp, "REST")) {
                break;
            }
            lex_next(&tmp);
        }
        {
            int len = (int)(tmp.token_start - expr_start);
            if (len >= (int)sizeof(actx.expr)) len = (int)sizeof(actx.expr) - 1;
            memcpy(actx.expr, expr_start, len);
            actx.expr[len] = '\0';
            trim_right(actx.expr);
        }
        if (actx.expr[0] == '\0') {
            printf("Syntax: SUM <expr> [scope] [FOR <cond>] [WHILE <cond>] [TO <var>]\n");
            return;
        }
        lexer_init_ext(l, tmp.token_start, l->store);
        if (parse_clauses(l, &c) < 0) return;
    }

    actx.total = 0.0;

    count = process_records(db, &c, PROC_USE_FILTER | PROC_SKIP_DELETED_SET, sum_cb, &actx);
    if (count < 0) return;
    if (count >= 0) {
        value_t v = val_num(actx.total);
        char buf[64];
        val_to_string(&v, buf, sizeof(buf));
        if (set_opts.talk) {
            printf("%d record(s) summed\n", count);
            printf("      SUM: %s\n", buf);
        }
        if (c.to_var[0]) {
            value_t tv = val_num(actx.total);
            memvar_set(&memvar_store, c.to_var, &tv);
        }
    }
}

static void cmd_average(dbf_t *db, lexer_t *l) {
    clause_t c;
    agg_ctx_t actx;
    int count;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    clause_init(&c);
    c.scope.type = SCOPE_ALL_DEFAULT;

    if (l->current.type == TOK_EOF) {
        printf("Syntax: AVERAGE <expr> [scope] [FOR <cond>] [WHILE <cond>]\n");
        return;
    }

    /* Optional leading scope (ALL/NEXT/RECORD/REST) */
    if (cmd_kw(l, "ALL") || cmd_kw(l, "NEXT") || cmd_kw(l, "RECORD") || cmd_kw(l, "REST")) {
        if (parse_clauses(l, &c) < 0) return;
    }

    /* Parse expression, then trailing clauses */
    {
        const char *expr_start = l->token_start;
        lexer_t tmp = *l;
        while (tmp.current.type != TOK_EOF) {
            if (cmd_kw(&tmp, "FOR") || cmd_kw(&tmp, "WHILE") ||
                cmd_kw(&tmp, "TO") ||
                cmd_kw(&tmp, "ALL") || cmd_kw(&tmp, "NEXT") ||
                cmd_kw(&tmp, "RECORD") || cmd_kw(&tmp, "REST")) {
                break;
            }
            lex_next(&tmp);
        }
        {
            int len = (int)(tmp.token_start - expr_start);
            if (len >= (int)sizeof(actx.expr)) len = (int)sizeof(actx.expr) - 1;
            memcpy(actx.expr, expr_start, len);
            actx.expr[len] = '\0';
            trim_right(actx.expr);
        }
        if (actx.expr[0] == '\0') {
            printf("Syntax: AVERAGE <expr> [scope] [FOR <cond>] [WHILE <cond>] [TO <var>]\n");
            return;
        }
        lexer_init_ext(l, tmp.token_start, l->store);
        if (parse_clauses(l, &c) < 0) return;
    }

    actx.total = 0.0;

    count = process_records(db, &c, PROC_USE_FILTER | PROC_SKIP_DELETED_SET, sum_cb, &actx);
    if (count < 0) return;
    if (count > 0) {
        value_t v = val_num(actx.total / count);
        char buf[64];
        val_to_string(&v, buf, sizeof(buf));
        if (set_opts.talk) {
            printf("%d record(s) averaged\n", count);
            printf("  AVERAGE: %s\n", buf);
        }
        if (c.to_var[0]) {
            value_t tv = val_num(actx.total / count);
            memvar_set(&memvar_store, c.to_var, &tv);
        }
    } else if (count == 0) {
        prog_error(ERR_RECORD_RANGE, "No records");
    }
}

/* ---- CALCULATE ---- */
enum { AGG_SUM, AGG_AVG, AGG_MIN, AGG_MAX, AGG_CNT };
#define MAX_CALC_AGGS 16

typedef struct {
    int naggs;
    struct {
        int type;
        char expr[256];
        double total;
        double minval, maxval;
        int has_value;
    } aggs[MAX_CALC_AGGS];
    char to_vars[MAX_CALC_AGGS][MEMVAR_NAMELEN];
    int nto;
} calc_ctx_t;

static int calculate_cb(dbf_t *db, uint32_t recno, void *userdata) {
    calc_ctx_t *ctx = (calc_ctx_t *)userdata;
    int i;
    (void)db; (void)recno;

    for (i = 0; i < ctx->naggs; i++) {
        value_t val;
        double v;
        if (ctx->aggs[i].type == AGG_CNT)
            continue;
        if (ast_eval_dynamic(ctx->aggs[i].expr, &expr_ctx, &val) != 0) {
            report_expr_error();
            return REC_ERROR;
        }
        if (val.type != VAL_NUM) {
            printf("CALCULATE requires numeric expression.\n");
            return REC_ERROR;
        }
        v = val.num;
        switch (ctx->aggs[i].type) {
        case AGG_SUM: case AGG_AVG:
            ctx->aggs[i].total += v;
            break;
        case AGG_MIN:
            if (!ctx->aggs[i].has_value || v < ctx->aggs[i].minval)
                ctx->aggs[i].minval = v;
            ctx->aggs[i].has_value = 1;
            break;
        case AGG_MAX:
            if (!ctx->aggs[i].has_value || v > ctx->aggs[i].maxval)
                ctx->aggs[i].maxval = v;
            ctx->aggs[i].has_value = 1;
            break;
        }
    }
    return REC_CONTINUE;
}

static void cmd_calculate(dbf_t *db, lexer_t *l) {
    clause_t c;
    calc_ctx_t ctx;
    int count, i;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    if (l->current.type == TOK_EOF) {
        printf("Syntax: CALCULATE <agg>(expr), ... TO <var>, ...\n");
        return;
    }

    memset(&ctx, 0, sizeof(ctx));

    /* Parse aggregate list: SUM(expr), AVG(expr), MIN(expr), MAX(expr), CNT() */
    for (;;) {
        int agg_type;
        if (l->current.type != TOK_IDENT) {
            printf("Expected aggregate function name.\n");
            return;
        }
        if (is_keyword(l->current.text, "SUM"))       agg_type = AGG_SUM;
        else if (is_keyword(l->current.text, "AVG") ||
                 is_keyword(l->current.text, "AVERAGE")) agg_type = AGG_AVG;
        else if (is_keyword(l->current.text, "MIN"))   agg_type = AGG_MIN;
        else if (is_keyword(l->current.text, "MAX"))   agg_type = AGG_MAX;
        else if (is_keyword(l->current.text, "CNT") ||
                 is_keyword(l->current.text, "COUNT"))  agg_type = AGG_CNT;
        else {
            printf("Unknown aggregate function: %s\n", l->current.text);
            return;
        }

        if (ctx.naggs >= MAX_CALC_AGGS) {
            printf("Too many aggregate expressions (max %d).\n", MAX_CALC_AGGS);
            return;
        }

        lex_next(l); /* consume function name */
        if (l->current.type != TOK_LPAREN) {
            printf("Expected '(' after aggregate function.\n");
            return;
        }

        /* Extract inner expression text from original input, then advance
           lexer past the matching ')'.
           We scan the raw string (not the lexer) because the multi-char
           operator lookahead in lex_next can trigger macro expansion as
           a side effect, making l->p unreliable for text capture. */
        {
            /* token_start points to '(' in the original input.
               Scan the raw string (not the lexer) to find the matching ')'.
               This avoids desync if macro expansion injects parentheses. */
            const char *raw = l->token_start + 1;
            int rdepth = 1;
            while (*raw && rdepth > 0) {
                if (*raw == '(') rdepth++;
                else if (*raw == ')') { rdepth--; if (rdepth == 0) break; }
                else if (*raw == '"' || *raw == '\'') {
                    char q = *raw++;
                    while (*raw) {
                        if (*raw == q) {
                            if (*(raw + 1) == q) { raw += 2; continue; }
                            break; /* closing quote */
                        }
                        raw++;
                    }
                }
                else if (*raw == '[') {
                    raw++;
                    while (*raw && *raw != ']') raw++;
                }
                if (*raw) raw++;
            }
            /* raw now points to matching ')' or '\0' */
            if (!*raw) {
                printf("Mismatched parentheses.\n");
                return;
            }
            {
                int len = (int)(raw - (l->token_start + 1));
                if (len >= (int)sizeof(ctx.aggs[0].expr))
                    len = (int)sizeof(ctx.aggs[0].expr) - 1;
                memcpy(ctx.aggs[ctx.naggs].expr, l->token_start + 1, len);
                ctx.aggs[ctx.naggs].expr[len] = '\0';
                trim_right(ctx.aggs[ctx.naggs].expr);
            }

            /* Reposition lexer past ')' in original input.
               This bypasses macro expansion entirely, avoiding
               desync if macros inject or remove parentheses. */
            l->p = raw + 1;
            l->macro_depth = 0;
        }

        if (agg_type != AGG_CNT && ctx.aggs[ctx.naggs].expr[0] == '\0') {
            printf("%s requires an expression.\n",
                   agg_type == AGG_SUM ? "SUM" :
                   agg_type == AGG_AVG ? "AVG" :
                   agg_type == AGG_MIN ? "MIN" : "MAX");
            return;
        }

        ctx.aggs[ctx.naggs].type = agg_type;
        ctx.naggs++;

        lex_next(l); /* consume ')' */

        if (l->current.type == TOK_COMMA) {
            lex_next(l); /* consume ',' */
        } else {
            break;
        }
    }

    /* Expect TO keyword */
    if (!cmd_kw(l, "TO")) {
        printf("Expected TO after aggregate list.\n");
        return;
    }
    lex_next(l); /* consume TO */

    /* Parse comma-separated variable names */
    ctx.nto = 0;
    for (;;) {
        if (l->current.type != TOK_IDENT || cmd_kw(l, "FOR") || cmd_kw(l, "WHILE") ||
            cmd_kw(l, "ALL") || cmd_kw(l, "NEXT") || cmd_kw(l, "RECORD") || cmd_kw(l, "REST")) {
            break;
        }
        if (ctx.nto >= MAX_CALC_AGGS) {
            printf("Too many TO variables.\n");
            return;
        }
        str_copy(ctx.to_vars[ctx.nto], l->current.text, MEMVAR_NAMELEN);
        ctx.nto++;
        lex_next(l);
        if (l->current.type == TOK_COMMA) {
            lex_next(l);
        } else {
            break;
        }
    }

    if (ctx.nto != ctx.naggs) {
        printf("Number of TO variables (%d) must match number of aggregates (%d).\n",
               ctx.nto, ctx.naggs);
        return;
    }

    /* Parse remaining clauses (FOR/WHILE/scope) */
    clause_init(&c);
    c.scope.type = SCOPE_ALL_DEFAULT;
    if (parse_clauses(l, &c) < 0) return;

    count = process_records(db, &c, PROC_USE_FILTER | PROC_SKIP_DELETED_SET, calculate_cb, &ctx);
    if (count < 0) return;

    /* Store results in memory variables */
    for (i = 0; i < ctx.naggs; i++) {
        value_t v;
        switch (ctx.aggs[i].type) {
        case AGG_SUM:
            v = val_num(ctx.aggs[i].total);
            break;
        case AGG_AVG:
            v = val_num(count > 0 ? ctx.aggs[i].total / count : 0.0);
            break;
        case AGG_MIN:
            v = val_num(ctx.aggs[i].has_value ? ctx.aggs[i].minval : 0.0);
            break;
        case AGG_MAX:
            v = val_num(ctx.aggs[i].has_value ? ctx.aggs[i].maxval : 0.0);
            break;
        case AGG_CNT:
            v = val_num((double)count);
            break;
        default:
            v = val_num(0.0);
            break;
        }
        memvar_set(&memvar_store, ctx.to_vars[i], &v);
    }
    printf("%d record(s)\n", count);
}

/* ---- REPLACE (enhanced with expr_eval) ---- */

#define MAX_REPLACE_PAIRS 32
typedef struct {
    int field_idx;
    char expr[256];      /* captured expression string */
    ast_node_t *ast;     /* pre-compiled AST, NULL if contains &macro */
} replace_pair_t;

typedef struct {
    replace_pair_t *pairs;
    int npairs;
} replace_ctx_t;

static int replace_capture_expr(lexer_t *l, char *out, int out_size) {
    const char *expr_start;
    int paren_depth = 0;
    int bracket_depth = 0;
    int expr_len;

    if (l->current.type == TOK_EOF) {
        prog_error(ERR_SYNTAX, "Expected expression after WITH");
        return -1;
    }

    expr_start = l->token_start;

    while (l->current.type != TOK_EOF) {
        if (paren_depth == 0 && bracket_depth == 0) {
            if (l->current.type == TOK_COMMA)
                break;
            if (l->current.type == TOK_IDENT &&
                (is_keyword(l->current.text, "FOR") ||
                 is_keyword(l->current.text, "WHILE") ||
                 is_keyword(l->current.text, "ALL") ||
                 is_keyword(l->current.text, "NEXT") ||
                 is_keyword(l->current.text, "RECORD") ||
                 is_keyword(l->current.text, "REST"))) {
                break;
            }
        }

        if (l->current.type == TOK_LPAREN) paren_depth++;
        else if (l->current.type == TOK_RPAREN && paren_depth > 0) paren_depth--;
        else if (l->current.type == TOK_LBRACKET) bracket_depth++;
        else if (l->current.type == TOK_RBRACKET && bracket_depth > 0) bracket_depth--;
        lex_next(l);
    }

    expr_len = (int)(l->token_start - expr_start);
    while (expr_len > 0 &&
           (expr_start[expr_len - 1] == ' ' || expr_start[expr_len - 1] == '\t'))
        expr_len--;

    if (expr_len <= 0) {
        prog_error(ERR_SYNTAX, "Expected expression after WITH");
        return -1;
    }

    if (expr_len >= out_size)
        expr_len = out_size - 1;
    memcpy(out, expr_start, expr_len);
    out[expr_len] = '\0';
    return 0;
}

static int replace_cb(dbf_t *db, uint32_t recno, void *userdata) {
    replace_ctx_t *rctx = userdata;
    char formatted[256];
    char old_keys[MAX_INDEXES][MAX_INDEX_KEY + 1];
    (void)recno;

    int has_indexes = (cur_wa()->num_indexes > 0);
    if (has_indexes)
        indexes_capture_keys(db, old_keys);

    memo_snapshot_t memo_snap;
    memo_snap.valid = 0;

    for (int p = 0; p < rctx->npairs; p++) {
        value_t val;
        char valbuf[256];

        if (rctx->pairs[p].ast) {
            if (ast_eval(rctx->pairs[p].ast, &expr_ctx, &val) != 0) {
                report_expr_error();
                return REC_ERROR;
            }
        } else {
            if (ast_eval_dynamic(rctx->pairs[p].expr, &expr_ctx, &val) != 0) {
                report_expr_error();
                return REC_ERROR;
            }
        }

        val_to_string(&val, valbuf, sizeof(valbuf));
        int idx = rctx->pairs[p].field_idx;

        switch (db->fields[idx].type) {
        case 'C':
            if (val.type == VAL_CHAR)
                field_format_char(formatted, db->fields[idx].length, val.str);
            else
                field_format_char(formatted, db->fields[idx].length, valbuf);
            break;
        case 'F':
        case 'N':
            field_format_numeric(formatted, db->fields[idx].length,
                                 db->fields[idx].decimals, valbuf);
            break;
        case 'D':
            if (val.type == VAL_DATE) date_to_dbf(val.date, formatted);
            else field_format_date(formatted, valbuf);
            break;
        case 'L':
            if (val.type == VAL_LOGIC) {
                formatted[0] = val.logic ? 'T' : 'F';
                formatted[1] = '\0';
            } else field_format_logical(formatted, valbuf);
            break;
        case 'M': {
            const char *memo_text = (val.type == VAL_CHAR) ? val.str : valbuf;
            if (!memo_snap.valid)
                dbf_memo_snapshot(db, &memo_snap);
            int nb = dbf_memo_write(db, memo_text, strlen(memo_text));
            if (nb > 0)
                snprintf(formatted, sizeof(formatted), "%10d", nb);
            else
                memset(formatted, ' ', 10);
            formatted[10] = '\0';
            break;
        }
        default:
            field_format_char(formatted, db->fields[idx].length, valbuf);
            break;
        }

        dbf_set_field_raw(db, idx, formatted);
    }

    /* Update indexes before flush — if UNIQUE violated, discard changes */
    if (has_indexes) {
        if (indexes_update_current(db, old_keys) != 0) {
            if (memo_snap.valid)
                dbf_memo_restore(db, &memo_snap);
            db->record_dirty = 0;
            dbf_read_record(db, db->current_record);
            return REC_CONTINUE;  /* skip, not fatal */
        }
    }

    dbf_flush_record(db);
    return REC_CONTINUE;
}

static void cmd_replace(dbf_t *db, lexer_t *l) {
    clause_t c;
    replace_pair_t pairs[MAX_REPLACE_PAIRS];
    int npairs = 0;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    clause_init(&c);
    /* dBase default for REPLACE is current record only (NEXT 1) if no clauses */
    c.scope.type = SCOPE_NEXT;
    c.scope.count = 1;

    /* For REPLACE, the field list/expression must come first, before clauses */
    const char *body_start = l->token_start;

    /* To find the end of the field list, we need to scan ahead for FOR/WHILE/ALL/NEXT etc. */
    lexer_t temp_l = *l;
    while (temp_l.current.type != TOK_EOF) {
        if (cmd_kw(&temp_l, "FOR") || cmd_kw(&temp_l, "WHILE") ||
            cmd_kw(&temp_l, "ALL") || cmd_kw(&temp_l, "NEXT") ||
            cmd_kw(&temp_l, "RECORD") || cmd_kw(&temp_l, "REST")) {
            break;
        }
        lex_next(&temp_l);
    }

    if (parse_clauses(&temp_l, &c) < 0) return;

    /* --- Pre-parse field/expression pairs (parse once) --- */
    {
        lexer_t parse_l;
        lexer_init_ext(&parse_l, body_start, expr_ctx.vars);

        while (parse_l.current.type == TOK_IDENT &&
               !is_keyword(parse_l.current.text, "FOR") &&
               !is_keyword(parse_l.current.text, "WHILE") &&
               !is_keyword(parse_l.current.text, "ALL") &&
               !is_keyword(parse_l.current.text, "NEXT") &&
               !is_keyword(parse_l.current.text, "RECORD") &&
               !is_keyword(parse_l.current.text, "REST")) {

            if (npairs >= MAX_REPLACE_PAIRS) {
                prog_error(ERR_SYNTAX, "Too many REPLACE fields");
                goto cleanup;
            }

            /* Field name lookup (once) */
            char field_name[DBF_MAX_FIELD_NAME];
            str_copy(field_name, parse_l.current.text, sizeof(field_name));
            str_upper(field_name);
            lex_next(&parse_l);

            pairs[npairs].field_idx = dbf_find_field(db, field_name);
            if (pairs[npairs].field_idx < 0) {
                prog_error_fmt(ERR_SYNTAX, "Field not found: %s", field_name);
                goto cleanup;
            }

            if (cmd_kw(&parse_l, "WITH")) lex_next(&parse_l);

            /* Capture expression boundaries without evaluating side effects. */
            if (replace_capture_expr(&parse_l, pairs[npairs].expr,
                                     sizeof(pairs[npairs].expr)) != 0) {
                goto cleanup;
            }

            /* Pre-compile AST if no macros */
            pairs[npairs].ast = NULL;
            if (!strchr(pairs[npairs].expr, '&')) {
                const char *error;
                pairs[npairs].ast = ast_compile(pairs[npairs].expr,
                                                expr_ctx.vars, &error);
            }

            lexer_init_ext(&parse_l, parse_l.token_start, expr_ctx.vars);
            if (parse_l.current.type == TOK_COMMA) lex_next(&parse_l);
            npairs++;
        }
    }

    replace_ctx_t rctx = { pairs, npairs };

    if (c.has_scope || c.for_cond[0] || c.while_cond[0]) {
        /* Explicit scope or conditions: use process_records for index traversal */
        process_records(db, &c, PROC_USE_FILTER | PROC_SKIP_DELETED_SET,
                        replace_cb, &rctx);
    } else {
        /* Default NEXT 1: replace current physical record directly */
        if (db->current_record > 0 && db->current_record <= db->record_count) {
            dbf_read_record(db, db->current_record);
            if (!skip_deleted(db->record_buf) && check_filter(db))
                replace_cb(db, db->current_record, &rctx);
        }
    }

    for (int p = 0; p < npairs; p++) {
        if (pairs[p].ast) ast_free(pairs[p].ast);
    }
    return;

cleanup:
    for (int p = 0; p < npairs; p++) {
        if (pairs[p].ast) ast_free(pairs[p].ast);
    }
}

/* ---- SCATTER / GATHER ---- */

/* Parse optional FIELDS field1,field2,... list into indices array.
 * Returns number of fields, or -1 on error.  If no FIELDS clause,
 * returns all fields. */
static int scatter_parse_fields(dbf_t *db, lexer_t *l,
                                int *indices, int max_fields) {
    int n = 0;
    if (cmd_kw(l, "FIELDS")) {
        lex_next(l);
        while (l->current.type == TOK_IDENT && n < max_fields) {
            char name[DBF_MAX_FIELD_NAME];
            str_copy(name, l->current.text, sizeof(name));
            str_upper(name);
            int idx = dbf_find_field(db, name);
            if (idx < 0) {
                prog_error_fmt(ERR_SYNTAX, "Field not found: %s", name);
                return -1;
            }
            indices[n++] = idx;
            lex_next(l);
            if (l->current.type == TOK_COMMA)
                lex_next(l);
        }
    } else {
        /* All fields */
        for (int i = 0; i < db->field_count && n < max_fields; i++)
            indices[n++] = i;
    }
    return n;
}

static void cmd_scatter(dbf_t *db, lexer_t *l) {
    int field_indices[DBF_MAX_FIELDS];
    int nfields;
    char raw[256];

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }
    if (db->current_record == 0) {
        prog_error(ERR_NO_DATABASE, "No current record");
        return;
    }

    /* Optional MEMVAR keyword (ignored — always scatters to memvars) */
    if (cmd_kw(l, "MEMVAR"))
        lex_next(l);

    nfields = scatter_parse_fields(db, l, field_indices, DBF_MAX_FIELDS);
    if (nfields < 0) return;

    for (int i = 0; i < nfields; i++) {
        int fi = field_indices[i];
        dbf_get_field_raw(db, fi, raw, sizeof(raw));
        value_t val;
        switch (db->fields[fi].type) {
        case 'C':
            trim_right(raw);
            val = val_str(raw);
            break;
        case 'F':
        case 'N':
            val = val_num(atof(raw));
            break;
        case 'D':
            val = val_date(date_from_dbf(raw));
            break;
        case 'L':
            val = val_logic(raw[0] == 'T' || raw[0] == 't');
            break;
        case 'M': {
            int blk = atoi(raw);
            char memo[256];
            if (blk > 0 && dbf_memo_read(db, blk, memo, sizeof(memo)) == 0)
                val = val_str(memo);
            else
                val = val_str("");
            break;
        }
        default:
            trim_right(raw);
            val = val_str(raw);
            break;
        }
        memvar_set(expr_ctx.vars, db->fields[fi].name, &val);
    }
}

static void cmd_gather(dbf_t *db, lexer_t *l) {
    int field_indices[DBF_MAX_FIELDS];
    int nfields;
    char formatted[256];
    char old_keys[MAX_INDEXES][MAX_INDEX_KEY + 1];
    int has_indexes;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }
    if (db->current_record == 0) {
        prog_error(ERR_NO_DATABASE, "No current record");
        return;
    }

    /* Optional FROM MEMVAR (ignored — always gathers from memvars) */
    if (cmd_kw(l, "FROM"))
        lex_next(l);
    if (cmd_kw(l, "MEMVAR"))
        lex_next(l);

    nfields = scatter_parse_fields(db, l, field_indices, DBF_MAX_FIELDS);
    if (nfields < 0) return;

    /* Capture keys before modification */
    has_indexes = (cur_wa()->num_indexes > 0);
    if (has_indexes)
        indexes_capture_keys(db, old_keys);

    memo_snapshot_t memo_snap;
    memo_snap.valid = 0;

    for (int i = 0; i < nfields; i++) {
        int fi = field_indices[i];
        value_t val;
        if (memvar_find(expr_ctx.vars, db->fields[fi].name, &val) != 0)
            continue;  /* No matching memvar — skip field */

        char valbuf[256];
        val_to_string(&val, valbuf, sizeof(valbuf));

        switch (db->fields[fi].type) {
        case 'C':
            if (val.type == VAL_CHAR)
                field_format_char(formatted, db->fields[fi].length, val.str);
            else
                field_format_char(formatted, db->fields[fi].length, valbuf);
            break;
        case 'F':
        case 'N':
            field_format_numeric(formatted, db->fields[fi].length,
                                 db->fields[fi].decimals, valbuf);
            break;
        case 'D':
            if (val.type == VAL_DATE) date_to_dbf(val.date, formatted);
            else field_format_date(formatted, valbuf);
            break;
        case 'L':
            if (val.type == VAL_LOGIC) {
                formatted[0] = val.logic ? 'T' : 'F';
                formatted[1] = '\0';
            } else field_format_logical(formatted, valbuf);
            break;
        case 'M': {
            const char *memo_text = (val.type == VAL_CHAR) ? val.str : valbuf;
            if (!memo_snap.valid)
                dbf_memo_snapshot(db, &memo_snap);
            int nb = dbf_memo_write(db, memo_text, strlen(memo_text));
            if (nb > 0)
                snprintf(formatted, sizeof(formatted), "%10d", nb);
            else
                memset(formatted, ' ', 10);
            formatted[10] = '\0';
            break;
        }
        default:
            field_format_char(formatted, db->fields[fi].length, valbuf);
            break;
        }

        dbf_set_field_raw(db, fi, formatted);
    }

    /* Update indexes */
    if (has_indexes) {
        if (indexes_update_current(db, old_keys) != 0) {
            if (memo_snap.valid)
                dbf_memo_restore(db, &memo_snap);
            db->record_dirty = 0;
            dbf_read_record(db, db->current_record);
            return;
        }
    }

    dbf_flush_record(db);
}

/* ---- DISPLAY (enhanced with field list) ---- */
static void common_list_display(dbf_t *db, lexer_t *l, int is_display) {
    int f;
    char raw[256], display[256];
    int field_indices[DBF_MAX_FIELDS];
    int nfields = 0;
    int use_all_fields;
    FILE *to_file = NULL;
    clause_t c;
    int row_count = 0;
    index_t *idx = controlling_index();
    uint32_t remaining = 0xFFFFFFFF;
    int has_limit = 0;
    uint32_t orig_rec = db->current_record;
    int orig_eof = expr_ctx.eof_flag;
    int orig_bof = expr_ctx.bof_flag;
    uint32_t last_rec = 0;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    if (db->record_count == 0) {
        prog_error(ERR_RECORD_RANGE, "No records");
        return;
    }

    clause_init(&c);

    /* Consume leading scope keyword before field list.
       dBase syntax: LIST/DISPLAY [ALL|NEXT n|RECORD n|REST] field1, field2 [FOR ...] */
    if (cmd_kw(l, "ALL")) {
        c.scope.type = SCOPE_ALL; c.has_scope = 1; lex_next(l);
    } else if (cmd_kw(l, "NEXT")) {
        lex_next(l);
        if (l->current.type == TOK_NUMBER) {
            c.scope.type = SCOPE_NEXT; c.scope.count = (uint32_t)l->current.num_val;
            c.has_scope = 1; lex_next(l);
        }
    } else if (cmd_kw(l, "RECORD")) {
        lex_next(l);
        if (l->current.type == TOK_NUMBER) {
            c.scope.type = SCOPE_RECORD; c.scope.count = (uint32_t)l->current.num_val;
            c.has_scope = 1; lex_next(l);
        }
    } else if (cmd_kw(l, "REST")) {
        c.scope.type = SCOPE_REST; c.has_scope = 1; lex_next(l);
    }

    /* Check if there's a field list before remaining clauses */
    if (l->current.type == TOK_IDENT &&
        !is_keyword(l->current.text, "FOR") &&
        !is_keyword(l->current.text, "WHILE") &&
        !is_keyword(l->current.text, "TO") &&
        !is_keyword(l->current.text, "OFF")) {

        const char *p = l->token_start;
        nfields = parse_field_list(db, p, field_indices, DBF_MAX_FIELDS, &p);
        if (nfields < 0) return;
        lexer_init_ext(l, p, l->store);
    }

    if (parse_clauses(l, &c) < 0) return;

    if (!c.has_scope) {
        if (is_display) {
            c.scope.type = SCOPE_NEXT;
            c.scope.count = 1;
        } else {
            c.scope.type = SCOPE_ALL_DEFAULT;
        }
    }

    if (c.for_cond[0] && !c.has_scope) {
        c.scope.type = SCOPE_ALL_DEFAULT;
    }
    if (c.scope.type == SCOPE_ALL_DEFAULT) {
        c.scope.type = SCOPE_ALL;
    } else if (c.scope.type == SCOPE_DEFAULT) {
        c.scope.type = SCOPE_NEXT;
        c.scope.count = 1;
    }

    /* DISPLAY without ALL/RECORD scope requires a current record */
    if (is_display && c.scope.type != SCOPE_ALL && c.scope.type != SCOPE_RECORD) {
        if (db->current_record == 0 || expr_ctx.eof_flag) {
            prog_error(ERR_RECORD_RANGE, "No current record");
            return;
        }
    }

    if (c.to_file[0]) {
        to_file = fopen(c.to_file, "w");
    }

    use_all_fields = (nfields == 0);

    /* Positioning based on scope */
    if (c.scope.type == SCOPE_ALL) {
        if (idx) index_top(idx);
        else dbf_read_record(db, 1);
    } else if (c.scope.type == SCOPE_RECORD) {
        if (idx) {
            /* This is inefficient but correctly follows index if RECORD N is used?
               Actually dBase RECORD N always uses physical record. */
            dbf_read_record(db, c.scope.count);
        } else {
            dbf_read_record(db, c.scope.count);
        }
        remaining = 1;
        has_limit = 1;
    } else if (c.scope.type == SCOPE_NEXT) {
        remaining = c.scope.count;
        has_limit = 1;
    }

    if (set_opts.heading && !(is_display && use_all_fields)) {
        if (!c.off) PRINT_TO(to_file, "Record#");
        if (use_all_fields) {
            for (f = 0; f < db->field_count; f++) {
                int w = field_display_width(db, f);
                PRINT_TO(to_file, " %-*s", w, db->fields[f].name);
            }
        } else {
            for (f = 0; f < nfields; f++) {
                int idx_f = field_indices[f];
                int w = field_display_width(db, idx_f);
                PRINT_TO(to_file, " %-*s", w, db->fields[idx_f].name);
            }
        }
        PRINT_TO(to_file, "\n");
    }

    expr_ctx.eof_flag = 0;
    while (!expr_ctx.eof_flag && (!has_limit || remaining > 0)) {
        uint32_t current_rec = idx ? index_current_recno(idx) : db->current_record;
        if (current_rec == 0 || current_rec > db->record_count) break;

        dbf_read_record(db, current_rec);

        int matched = 1;
        if (skip_deleted(db->record_buf) || !check_filter(db)) matched = 0;

        if (matched && c.for_cond[0]) {
            value_t cond;
            if (expr_eval_str(&expr_ctx, c.for_cond, &cond) != 0) { report_expr_error(); break; }
            if (cond.type != VAL_LOGIC || !cond.logic) matched = 0;
        }

        if (matched && c.while_cond[0]) {
            value_t cond;
            if (expr_eval_str(&expr_ctx, c.while_cond, &cond) != 0) { report_expr_error(); break; }
            if (cond.type != VAL_LOGIC || !cond.logic) break;
        }

        if (matched) {
            if (is_display && !to_file && row_count > 0 && (row_count % 20) == 0) {
                printf("Press any key to continue...");
                fflush(stdout);
                getchar();
                printf("\r                           \r");
            }

            if (is_display && use_all_fields) {
                PRINT_TO(to_file, "Record# %d\n", (int)current_rec);
                for (f = 0; f < db->field_count; f++)
                    print_field_value(db, f, raw, display, to_file, 0, 0);
            } else {
                if (!c.off) PRINT_TO(to_file, "%7d", (int)current_rec);
                if (use_all_fields) {
                    for (f = 0; f < db->field_count; f++) {
                        int w = field_display_width(db, f);
                        print_field_value(db, f, raw, display, to_file, 1, w);
                    }
                } else {
                    for (f = 0; f < nfields; f++) {
                        int idx_f = field_indices[f];
                        int w = field_display_width(db, idx_f);
                        print_field_value(db, idx_f, raw, display, to_file, 1, w);
                    }
                }
                PRINT_TO(to_file, "\n");
            }
            last_rec = current_rec;
            row_count++;
        }

        /* NEXT N counts records examined, not records matched */
        if (has_limit) remaining--;

        /* Advance */
        if (idx) {
            if (index_next(idx) < 0) expr_ctx.eof_flag = 1;
        } else {
            if (db->current_record >= db->record_count) expr_ctx.eof_flag = 1;
            else dbf_read_record(db, db->current_record + 1);
        }
        if (c.scope.type == SCOPE_RECORD) break;
    }

    if (to_file) fclose(to_file);

    /* Restore current record to last displayed, preserving pre-list flags. */
    if (last_rec > 0)
        dbf_read_record(db, last_rec);
    else if (orig_rec > 0)
        dbf_read_record(db, orig_rec);
    expr_ctx.eof_flag = orig_eof;
    expr_ctx.bof_flag = orig_bof;
}

static void cmd_list(dbf_t *db, lexer_t *l) {
    common_list_display(db, l, 0);
}

static void cmd_display(dbf_t *db, lexer_t *l) {
    common_list_display(db, l, 1);
}

/* ---- DISPLAY STRUCTURE ---- */
static void cmd_display_structure(dbf_t *db) {
    int i;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    printf("Structure for database: %s\n", db->filename);
    printf("Number of records: %d\n", (int)db->record_count);
    printf("Header size: %d\n", (int)db->header_size);
    printf("Record size: %d\n", (int)db->record_size);
    printf("Field  Name        Type  Width  Dec\n");
    for (i = 0; i < db->field_count; i++) {
        printf("%5d  %-10s  %c      %2d      %d\n",
               i + 1, db->fields[i].name, db->fields[i].type,
               db->fields[i].length, db->fields[i].decimals);
    }
    printf("** Total **                  %2d\n", (int)db->record_size);
}

/* ---- DISPLAY STATUS ---- */
static void cmd_display_status(void) {
    int i, any_open = 0;

    printf("Currently selected database:\n");
    for (i = 0; i < MAX_AREAS; i++) {
        work_area_t *wa = area_get(i);
        if (!dbf_is_open(&wa->db)) continue;
        any_open = 1;

        printf("  Select area: %d, Database in use: %s",
               i + 1, wa->db.filename);
        if (wa->alias[0])
            printf("  Alias: %s", wa->alias);
        printf("\n");

        /* Show indexes */
        if (wa->num_indexes > 0) {
            int j;
            /* Show controlling index first */
            if (wa->order > 0 && wa->order <= wa->num_indexes) {
                index_t *idx = &wa->indexes[wa->order - 1];
                printf("    Master index file: %s  Key: %s\n",
                       idx->filename, idx->key_expr);
            }
            /* Show remaining indexes */
            for (j = 0; j < wa->num_indexes; j++) {
                if (j + 1 == wa->order) continue; /* skip master, already shown */
                printf("    Index file: %s  Key: %s\n",
                       wa->indexes[j].filename, wa->indexes[j].key_expr);
            }
        }

        /* Show filter */
        if (wa->filter_cond[0])
            printf("    Filter: %s\n", wa->filter_cond);

        /* Show relation */
        if (wa->relation_expr[0])
            printf("    Relation: %s into area %d\n",
                   wa->relation_expr, wa->relation_target + 1);
    }

    if (!any_open)
        printf("  No database is in use\n");

    printf("\nFile search path:\n  (none)\n");
    printf("\nDefault disk drive: .\n");

    /* Procedure file */
    {
        const char *proc = prog_get_procedure_name();
        if (proc)
            printf("\nProcedure file: %s\n", proc);
    }

    printf("\n");
    set_display(&set_opts);
}

/* ---- STORE expr TO var ---- */
static void cmd_store(dbf_t *db, lexer_t *l) {
    char expr_str[256];
    value_t val;
    char name[MEMVAR_NAMELEN];

    (void)db;

    /* Expression start */
    const char *expr_start = l->token_start;
    lexer_t tmp = *l;
    while (tmp.current.type != TOK_EOF && !cmd_kw(&tmp, "TO"))
        lex_next(&tmp);
    
    if (!cmd_kw(&tmp, "TO")) {
        printf("Syntax: STORE <expr> TO <variable>\n");
        return;
    }

    int len = (int)(tmp.token_start - expr_start);
    if (len >= (int)sizeof(expr_str)) len = (int)sizeof(expr_str) - 1;
    memcpy(expr_str, expr_start, len);
    expr_str[len] = '\0';
    trim_right(expr_str);

    if (expr_eval_str(&expr_ctx, expr_str, &val) != 0) {
        report_expr_error();
        return;
    }

    lex_next(&tmp); /* skip TO */
    if (tmp.current.type != TOK_IDENT) {
        printf("Syntax: STORE <expr> TO <variable>\n");
        return;
    }
    str_copy(name, tmp.current.text, sizeof(name));
    str_upper(name);

    lex_next(&tmp);
    int is_array = 0;
    value_t mv;
    if (memvar_find(&memvar_store, name, &mv) == 0 && mv.type == VAL_ARRAY)
        is_array = 1;

    if (is_array && tmp.current.type != TOK_LPAREN && tmp.current.type != TOK_LBRACKET) {
        prog_error(ERR_SYNTAX, "Array assignment requires index");
        return;
    }

    if ((is_array && (tmp.current.type == TOK_LPAREN || tmp.current.type == TOK_LBRACKET)) ||
        (tmp.current.type == TOK_LBRACKET)) {
        token_type_t end_type = (tmp.current.type == TOK_LPAREN) ? TOK_RPAREN : TOK_RBRACKET;
        const char *next_p = tmp.token_start + 1; /* skip [ or ( */
        value_t row_v, col_v;
        int row = 0, col = 1;
        if (expr_eval(&expr_ctx, &next_p, &row_v) == 0 && row_v.type == VAL_NUM) {
            row = (int)row_v.num;
            lexer_init_ext(&tmp, next_p, expr_ctx.vars);
            if (tmp.current.type == TOK_COMMA) {
                next_p = tmp.token_start + 1; /* skip , */
                if (expr_eval(&expr_ctx, &next_p, &col_v) == 0 && col_v.type == VAL_NUM) {
                    col = (int)col_v.num;
                    lexer_init_ext(&tmp, next_p, expr_ctx.vars);
                }
            }
            if (tmp.current.type == end_type) {
                lex_next(&tmp);
            }
            int res = memvar_set_elem(&memvar_store, name, row, col, &val);
            if (res == -2) {
                prog_error(ERR_RECORD_RANGE, "Array index out of bounds");
            } else if (res == -1) {
                prog_error(ERR_SYNTAX, "Variable is not an array");
            }
            return;
        }
    }

    if (lex_is_reserved(name)) {
        char err[128];
        snprintf(err, sizeof(err), "%s is a reserved keyword", name);
        prog_error(ERR_SYNTAX, err);
        return;
    }

    memvar_set(&memvar_store, name, &val);
}

/* ---- RELEASE ---- */
static void cmd_release(const char *arg) {
    const char *p = skip_ws(arg);
    char name[MEMVAR_NAMELEN];
    int i;

    if (str_imatch(p, "ALL")) {
        const char *q = skip_ws(p + 3);
        if (str_imatch(q, "LIKE")) {
            q = skip_ws(q + 4);
            char pat[MEMVAR_NAMELEN];
            int pi = 0;
            while (*q && *q != ' ' && *q != '\t' && pi < MEMVAR_NAMELEN - 1)
                pat[pi++] = *q++;
            pat[pi] = '\0';
            str_upper(pat);
            memvar_release_matching(&memvar_store, pat, 1);
        } else if (str_imatch(q, "EXCEPT")) {
            q = skip_ws(q + 6);
            char pat[MEMVAR_NAMELEN];
            int pi = 0;
            while (*q && *q != ' ' && *q != '\t' && pi < MEMVAR_NAMELEN - 1)
                pat[pi++] = *q++;
            pat[pi] = '\0';
            str_upper(pat);
            memvar_release_matching(&memvar_store, pat, 0);
        } else {
            memvar_release_all(&memvar_store);
        }
        return;
    }

    while (*p) {
        i = 0;
        while (is_ident_char(*p) && i < MEMVAR_NAMELEN - 1)
            name[i++] = *p++;
        name[i] = '\0';

        if (name[0] != '\0') {
            if (memvar_release(&memvar_store, name) < 0)
                prog_error_fmt(ERR_VAR_NOT_FOUND, "Variable not found: %s", name);
        }

        p = skip_ws(p);
        if (*p == ',') { p++; p = skip_ws(p); }
        else break;
    }
}

/* ---- DELETE [scope] [FOR cond] ---- */
static int delete_cb(dbf_t *db, uint32_t recno, void *userdata) {
    (void)recno; (void)userdata;
    db->record_buf[0] = '*';
    db->record_dirty = 1;
    dbf_flush_record(db);
    return REC_CONTINUE;
}

static int recall_cb(dbf_t *db, uint32_t recno, void *userdata) {
    (void)recno; (void)userdata;
    db->record_buf[0] = ' ';
    db->record_dirty = 1;
    dbf_flush_record(db);
    return REC_CONTINUE;
}

static void cmd_delete(dbf_t *db, lexer_t *l) {
    clause_t c;
    int count;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    if (l->current.type == TOK_EOF) {
        if (db->current_record == 0 || expr_ctx.eof_flag) {
            prog_error(ERR_RECORD_RANGE, "No current record");
            return;
        }
        db->record_buf[0] = '*';
        db->record_dirty = 1;
        dbf_flush_record(db);
        if (set_opts.talk) printf("1 record deleted.\n");
        return;
    }

    clause_init(&c);
    c.scope.type = SCOPE_DEFAULT;
    if (parse_clauses(l, &c) < 0) return;

    count = process_records(db, &c, PROC_SKIP_DELETED_ANY | PROC_LEAVE_ON_MATCH, delete_cb, NULL);
    if (count < 0) return;
    if (set_opts.talk) {
        if (count == 1) printf("1 record deleted.\n");
        else printf("%d record(s) deleted.\n", count);
    }
}

static void cmd_recall(dbf_t *db, lexer_t *l) {
    clause_t c;
    int count;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    if (l->current.type == TOK_EOF) {
        if (db->current_record == 0 || expr_ctx.eof_flag) {
            prog_error(ERR_RECORD_RANGE, "No current record");
            return;
        }
        if (db->record_buf[0] == '*') {
            db->record_buf[0] = ' ';
            db->record_dirty = 1;
            dbf_flush_record(db);
        }
        if (set_opts.talk) printf("1 record recalled.\n");
        return;
    }

    clause_init(&c);
    c.scope.type = SCOPE_DEFAULT;
    if (parse_clauses(l, &c) < 0) return;

    count = process_records(db, &c, PROC_REQUIRE_DELETED | PROC_LEAVE_ON_MATCH, recall_cb, NULL);
    if (count < 0) return;

    if (set_opts.talk) {
        if (count == 1) printf("1 record recalled.\n");
        else printf("%d record(s) recalled.\n", count);
    }
}

/* ---- PACK ---- */
static void cmd_pack(dbf_t *db) {
    uint32_t src, dst;
    int rec_size;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    rec_size = db->record_size;
    dst = 0;

    for (src = 1; src <= db->record_count; src++) {
        dbf_read_record(db, src);
        if (db->record_buf[0] == '*') continue;
        dst++;
        if (dst != src) {
            long pos = db->header_size + (long)(dst - 1) * rec_size;
            fseek(db->fp, pos, 0);
            fwrite(db->record_buf, 1, rec_size, db->fp);
        }
    }

    db->record_count = dst;
    dbf_write_header_counts(db);
    fflush(db->fp);
    area_invalidate_all(db->filename);

    if (dst > 0) {
        dbf_read_record(db, 1);
        expr_ctx.eof_flag = 0;
    } else {
        db->current_record = 0;
        expr_ctx.eof_flag = 1;
    }

    /* Rebuild all indexes with new record numbers */
    if (cur_wa()->num_indexes > 0)
        indexes_rebuild_all(db);

    if (set_opts.talk) printf("%d record(s) remaining.\n", (int)dst);
}

/* ---- ZAP ---- */
static void cmd_zap(dbf_t *db) {
    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    db->record_count = 0;
    db->current_record = 0;
    db->record_dirty = 0;
    dbf_write_header_counts(db);
    area_invalidate_all(db->filename);
    expr_ctx.eof_flag = 1;

    /* Clear all active indexes */
    if (cur_wa()->num_indexes > 0)
        indexes_clear_all();

    /* Reset .DBT memo file header */
    if (db->memo_fp) {
        db->next_memo_block = 1;
    }

    if (set_opts.talk) printf("Zap complete.\n");
}

/* ---- ACCEPT ---- */
static void cmd_accept(const char *arg) {
    const char *p = skip_ws(arg);
    char line[256];
    char name[MEMVAR_NAMELEN];
    int i;
    value_t val;

    if (*p == '"' || *p == '\'') {
        char quote = *p++;
        while (*p && *p != quote) {
            putchar(*p);
            p++;
        }
        if (*p == quote) p++;
    }

    p = skip_ws(p);
    if (!str_imatch(p, "TO")) {
        printf("Syntax: ACCEPT [\"prompt\"] TO <variable>\n");
        return;
    }
    p = skip_ws(p + 2);

    i = 0;
    while (is_ident_char(*p) && i < MEMVAR_NAMELEN - 1)
        name[i++] = *p++;
    name[i] = '\0';

    if (name[0] == '\0') {
        printf("Syntax: ACCEPT [\"prompt\"] TO <variable>\n");
        return;
    }
    if (lex_is_reserved(name)) {
        char err[128];
        snprintf(err, sizeof(err), "%s is a reserved keyword", name);
        prog_error(ERR_SYNTAX, err);
        return;
    }

    if (read_line(line, sizeof(line)) < 0)
        line[0] = '\0';

    val = val_str(line);
    memvar_set(&memvar_store, name, &val);
}

/* ---- INPUT ---- */
static void cmd_input(const char *arg) {
    const char *p = skip_ws(arg);
    char line[256];
    char name[MEMVAR_NAMELEN];
    int i;
    value_t val;

    if (*p == '"' || *p == '\'') {
        char quote = *p++;
        while (*p && *p != quote) {
            putchar(*p);
            p++;
        }
        if (*p == quote) p++;
    }

    p = skip_ws(p);
    if (!str_imatch(p, "TO")) {
        printf("Syntax: INPUT [\"prompt\"] TO <variable>\n");
        return;
    }
    p = skip_ws(p + 2);

    i = 0;
    while (is_ident_char(*p) && i < MEMVAR_NAMELEN - 1)
        name[i++] = *p++;
    name[i] = '\0';

    if (name[0] == '\0') {
        printf("Syntax: INPUT [\"prompt\"] TO <variable>\n");
        return;
    }
    if (lex_is_reserved(name)) {
        char err[128];
        snprintf(err, sizeof(err), "%s is a reserved keyword", name);
        prog_error(ERR_SYNTAX, err);
        return;
    }

    if (read_line(line, sizeof(line)) < 0)
        line[0] = '\0';

    ctx_setup();
    if (expr_eval_str(&expr_ctx, line, &val) != 0) {
        report_expr_error();
        val = val_num(0);
    }

    memvar_set(&memvar_store, name, &val);
}

/* ---- WAIT ---- */
static void cmd_wait(const char *arg) {
    const char *p = skip_ws(arg);
    char name[MEMVAR_NAMELEN];
    int has_to = 0;
    int c;

    if (*p == '"' || *p == '\'') {
        char quote = *p++;
        while (*p && *p != quote) {
            putchar(*p);
            p++;
        }
        if (*p == quote) p++;
        p = skip_ws(p);
    } else if (*p == '\0' || str_imatch(p, "TO")) {
        printf("Press any key to continue...");
    }

    fflush(stdout);

    if (str_imatch(p, "TO")) {
        int i;
        p = skip_ws(p + 2);
        i = 0;
        while (is_ident_char(*p) && i < MEMVAR_NAMELEN - 1)
            name[i++] = *p++;
        name[i] = '\0';
        if (name[0] != '\0') {
            if (lex_is_reserved(name)) {
                char err[128];
                snprintf(err, sizeof(err), "%s is a reserved keyword", name);
                prog_error(ERR_SYNTAX, err);
                return;
            }
            has_to = 1;
        }
    }

    c = getchar();
    if (c == -1) c = 0;
    if (c != '\n') {
        int next = getchar();
        (void)next;
    }
    screen_set_lastkey((c == '\n') ? 13 : c);
    screen_check_key_handler((c == '\n') ? 13 : c);

    if (has_to) {
        char ch[2];
        value_t val;
        ch[0] = (c == '\n') ? ' ' : (char)c;
        ch[1] = '\0';
        val = val_str(ch);
        memvar_set(&memvar_store, name, &val);
    }
    printf("\n");
}

/* ---- ? / ?? (print expressions) ---- */
static void cmd_print_expr(const char *arg, int newline) {
    const char *p;
    int first = 1;
    int is_print = (cmd_get_device() == 1);

    p = skip_ws(arg);

    if (*p == '\0') {
        if (newline) {
            if (is_print) screen_print_newline();
            else cmd_output("\n");
        }
        return;
    }

    while (*p) {
        value_t val;
        char buf[256];

        p = skip_ws(p);
        if (*p == '\0') break;

        if (expr_eval(&expr_ctx, &p, &val) != 0) {
            report_expr_error();
            return;
        }

        val_to_string(&val, buf, sizeof(buf));
        if (!first) {
            if (is_print) screen_print_text(" ");
            else cmd_output(" ");
        }
        if (is_print) screen_print_text(buf);
        else cmd_output(buf);
        first = 0;

        p = skip_ws(p);
        if (*p == ',') {
            p++;
        }
    }

    if (newline) {
        if (is_print) screen_print_newline();
        else cmd_output("\n");
    }
}

/* ---- Helper: copy memo field from source to dest ---- */
static void copy_memo_field(dbf_t *src, int si, dbf_t *dest, int di) {
    char raw[16], memo[256];
    int blk;
    dbf_get_field_raw(src, si, raw, sizeof(raw));
    blk = atoi(raw);
    if (blk > 0 && dbf_memo_read(src, blk, memo, sizeof(memo)) == 0) {
        int nb = dbf_memo_write(dest, memo, strlen(memo));
        if (nb > 0) {
            char bs[16];
            snprintf(bs, sizeof(bs), "%10d", nb);
            dbf_set_field_raw(dest, di, bs);
        }
    }
}

/* Forward declarations for helpers defined later (used by APPEND FROM) */
static const char *parse_filename_raw(const char *arg, char *filename, int size);
static int has_extension(const char *filename);

/* ---- COPY TO SDF helper ---- */
static void copy_to_sdf(dbf_t *db, const char *filename,
                         int *field_indices, int nfields,
                         int use_all_fields,
                         uint32_t start, uint32_t end,
                         const char *cond_str)
{
    FILE *fp;
    uint32_t i;
    int f, fc, count = 0;

    fp = fopen(filename, "wb");
    if (!fp) {
        prog_error_fmt(ERR_CANNOT_CREATE, "Error creating %s", filename);
        return;
    }

    fc = use_all_fields ? db->field_count : nfields;

    for (i = start; i <= end; i++) {
        char raw[256];

        dbf_read_record(db, i);
        if (skip_deleted(db->record_buf) || !check_filter(db)) continue;

        if (cond_str) {
            value_t cond;
            if (expr_eval_str(&expr_ctx, cond_str, &cond) != 0) {
                report_expr_error();
                fclose(fp);
                return;
            }
            if (cond.type != VAL_LOGIC || !cond.logic) continue;
        }

        for (f = 0; f < fc; f++) {
            int fi = use_all_fields ? f : field_indices[f];
            char type = db->fields[fi].type;
            int flen = db->fields[fi].length;
            int j;

            if (type == 'M') {
                /* Memo: write spaces to field width (typically 10) */
                for (j = 0; j < flen; j++) fputc(' ', fp);
                continue;
            }

            dbf_get_field_raw(db, fi, raw, sizeof(raw));

            if (type == 'D') {
                /* Date: write as YYYYMMDD (8 chars) */
                for (j = 0; j < 8 && raw[j]; j++) fputc(raw[j], fp);
                for (; j < 8; j++) fputc(' ', fp);
            } else {
                /* C, N, F, L: write raw, padded to field length */
                int len = strlen(raw);
                if (len > flen) len = flen;
                for (j = 0; j < len; j++) fputc(raw[j], fp);
                for (; j < flen; j++) fputc(' ', fp);
            }
        }

        fputc('\r', fp);
        fputc('\n', fp);
        count++;
    }

    fclose(fp);
    if (set_opts.talk) printf("%d record(s) copied.\n", count);
}

/* ---- COPY TO DELIMITED helper ---- */
static void copy_to_delimited(dbf_t *db, const char *filename,
                               int *field_indices, int nfields,
                               int use_all_fields,
                               uint32_t start, uint32_t end,
                               const char *cond_str,
                               char quote_char, int blank_mode)
{
    FILE *fp;
    uint32_t i;
    int f, fc, count = 0;

    fp = fopen(filename, "wb");
    if (!fp) {
        prog_error_fmt(ERR_CANNOT_CREATE, "Error creating %s", filename);
        return;
    }

    fc = use_all_fields ? db->field_count : nfields;

    for (i = start; i <= end; i++) {
        char raw[256];
        int first = 1;

        dbf_read_record(db, i);
        if (skip_deleted(db->record_buf) || !check_filter(db)) continue;

        if (cond_str) {
            value_t cond;
            if (expr_eval_str(&expr_ctx, cond_str, &cond) != 0) {
                report_expr_error();
                fclose(fp);
                return;
            }
            if (cond.type != VAL_LOGIC || !cond.logic) continue;
        }

        for (f = 0; f < fc; f++) {
            int fi = use_all_fields ? f : field_indices[f];
            char type = db->fields[fi].type;
            int len;

            if (type == 'M') continue; /* skip memo fields */

            if (!first)
                fputc(blank_mode ? ' ' : ',', fp);
            first = 0;

            dbf_get_field_raw(db, fi, raw, sizeof(raw));

            /* Trim trailing spaces */
            len = strlen(raw);
            while (len > 0 && raw[len - 1] == ' ') len--;
            raw[len] = '\0';

            /* Trim leading spaces for non-character fields */
            if (type != 'C') {
                char *s = raw;
                while (*s == ' ') s++;
                if (s != raw) memmove(raw, s, strlen(s) + 1);
                len = strlen(raw);
            }

            if (type == 'C' && !blank_mode) {
                /* Character: quoted, embedded quotes doubled */
                int j;
                fputc(quote_char, fp);
                for (j = 0; j < len; j++) {
                    if (raw[j] == quote_char)
                        fputc(quote_char, fp);
                    fputc(raw[j], fp);
                }
                fputc(quote_char, fp);
            } else {
                /* Numeric, Date, Logical, or BLANK mode: unquoted */
                fputs(raw, fp);
            }
        }

        fputc('\r', fp);
        fputc('\n', fp);
        count++;
    }

    fclose(fp);
    if (set_opts.talk) printf("%d record(s) copied.\n", count);
}

/* ---- COPY TO [FIELDS ...] [scope] [DELIMITED] [FOR cond] ---- */
static void cmd_copy_to(dbf_t *db, const char *arg) {
    char filename[64];
    const char *p;
    int field_indices[DBF_MAX_FIELDS];
    int nfields = 0;
    int use_all_fields;
    scope_t scope;
    const char *cond_str = NULL;
    uint32_t start, end, i;
    dbf_t dest;
    dbf_field_t dest_fields[DBF_MAX_FIELDS];
    int dest_nfields;
    int count = 0;
    int f;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    p = parse_filename_raw(arg, filename, sizeof(filename));

    if (filename[0] == '\0') {
        printf("Syntax: COPY TO <filename> [FIELDS f1,f2,...] [scope] [DELIMITED [WITH <char>|BLANK]] [FOR cond]\n");
        return;
    }

    p = skip_ws(p);

    /* Parse optional FIELDS clause */
    if (str_imatch(p, "FIELDS")) {
        p = skip_ws(p + 6);
        nfields = parse_field_list(db, p, field_indices, DBF_MAX_FIELDS, &p);
        if (nfields < 0) return;
        p = skip_ws(p);
    }

    /* Parse scope */
    scope = parse_scope(&p);

    /* Check for DELIMITED keyword */
    p = skip_ws(p);
    if (str_imatch(p, "DELIMITED")) {
        int blank_mode = 0;
        char quote_char = '"';
        p = skip_ws(p + 9);

        if (str_imatch(p, "WITH")) {
            p = skip_ws(p + 4);
            if (str_imatch(p, "BLANK")) {
                blank_mode = 1;
                p = skip_ws(p + 5);
            } else if (*p) {
                quote_char = *p++;
                p = skip_ws(p);
            }
        }

        if (str_imatch(p, "FOR"))
            cond_str = skip_ws(p + 3);

        if (!has_extension(filename))
            strcat(filename, ".TXT");

        use_all_fields = (nfields == 0);

        if (scope_bounds(db, &scope, &start, &end) < 0) {
            prog_error(ERR_RECORD_RANGE, "Invalid scope");
            return;
        }

        copy_to_delimited(db, filename, field_indices, nfields, use_all_fields,
                          start, end, cond_str, quote_char, blank_mode);
        return;
    }

    /* Check for SDF keyword */
    if (str_imatch(p, "SDF") || (str_imatch(p, "TYPE") && str_imatch(skip_ws(p + 4), "SDF"))) {
        if (str_imatch(p, "TYPE")) p = skip_ws(skip_ws(p + 4) + 3);
        else p = skip_ws(p + 3);

        if (str_imatch(p, "FOR"))
            cond_str = skip_ws(p + 3);

        if (!has_extension(filename))
            strcat(filename, ".TXT");

        use_all_fields = (nfields == 0);

        if (scope_bounds(db, &scope, &start, &end) < 0) {
            prog_error(ERR_RECORD_RANGE, "Invalid scope");
            return;
        }

        copy_to_sdf(db, filename, field_indices, nfields, use_all_fields,
                     start, end, cond_str);
        return;
    }

    /* Parse FOR */
    if (str_imatch(p, "FOR")) {
        cond_str = skip_ws(p + 3);
    }

    use_all_fields = (nfields == 0);

    /* Add .DBF extension if none provided */
    if (!has_extension(filename))
        strcat(filename, ".DBF");

    /* Build dest field list */
    if (use_all_fields) {
        dest_nfields = db->field_count;
        for (f = 0; f < dest_nfields; f++)
            dest_fields[f] = db->fields[f];
    } else {
        dest_nfields = nfields;
        for (f = 0; f < nfields; f++)
            dest_fields[f] = db->fields[field_indices[f]];
    }

    /* Recompute offsets for dest */
    {
        uint16_t off = 0;
        for (f = 0; f < dest_nfields; f++) {
            dest_fields[f].offset = off;
            off += dest_fields[f].length;
        }
    }

    if (dbf_create(filename, dest_fields, dest_nfields) < 0) {
        prog_error_fmt(ERR_CANNOT_CREATE, "Error creating %s", filename);
        return;
    }

    /* Create .DBT if destination has memo fields */
    {
        int mi;
        for (mi = 0; mi < dest_nfields; mi++) {
            if (dest_fields[mi].type == 'M') {
                dbf_memo_create(filename);
                break;
            }
        }
    }

    dbf_init(&dest);
    if (dbf_open(&dest, filename) < 0) {
        prog_error_fmt(ERR_CANNOT_OPEN, "Error opening %s", filename);
        return;
    }

    if (scope_bounds(db, &scope, &start, &end) < 0) {
        prog_error(ERR_RECORD_RANGE, "Invalid scope");
        dbf_close(&dest);
        return;
    }

    for (i = start; i <= end; i++) {
        char raw[256];
        dbf_read_record(db, i);
        if (skip_deleted(db->record_buf) || !check_filter(db)) continue;

        if (cond_str) {
            value_t cond;
            if (expr_eval_str(&expr_ctx, cond_str, &cond) != 0) {
                report_expr_error();
                dbf_close(&dest);
                return;
            }
            if (cond.type != VAL_LOGIC || !cond.logic) continue;
        }

        dbf_append_blank(&dest);

        if (use_all_fields) {
            for (f = 0; f < db->field_count; f++) {
                if (db->fields[f].type == 'M')
                    copy_memo_field(db, f, &dest, f);
                else {
                    dbf_get_field_raw(db, f, raw, sizeof(raw));
                    dbf_set_field_raw(&dest, f, raw);
                }
            }
        } else {
            for (f = 0; f < nfields; f++) {
                if (db->fields[field_indices[f]].type == 'M')
                    copy_memo_field(db, field_indices[f], &dest, f);
                else {
                    dbf_get_field_raw(db, field_indices[f], raw, sizeof(raw));
                    dbf_set_field_raw(&dest, f, raw);
                }
            }
        }
        dbf_flush_record(&dest);
        count++;
    }

    dbf_close(&dest);
    if (set_opts.talk) printf("%d record(s) copied.\n", count);
}

/* ---- COPY STRUCTURE TO ---- */
static void cmd_copy_structure(dbf_t *db, const char *arg) {
    char filename[64];

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    parse_filename(arg, filename, sizeof(filename));

    if (filename[0] == '\0' || str_icmp(filename, ".DBF") == 0) {
        printf("Syntax: COPY STRUCTURE TO <filename>\n");
        return;
    }

    /* Recompute offsets for create */
    {
        dbf_field_t fields[DBF_MAX_FIELDS];
        int f;
        uint16_t off = 0;
        for (f = 0; f < db->field_count; f++) {
            fields[f] = db->fields[f];
            fields[f].offset = off;
            off += fields[f].length;
        }
        if (dbf_create(filename, fields, db->field_count) < 0) {
            prog_error_fmt(ERR_CANNOT_CREATE, "Error creating %s", filename);
            return;
        }

        /* Create .DBT if any memo fields */
        for (f = 0; f < db->field_count; f++) {
            if (fields[f].type == 'M') {
                dbf_memo_create(filename);
                break;
            }
        }
    }

    printf("Structure copied to %s.\n", filename);
}

/* ---- COPY STRUCTURE EXTENDED TO ---- */
static void cmd_copy_structure_extended(dbf_t *db, const char *arg) {
    char filename[64];
    dbf_field_t ext_fields[4];
    dbf_t dest;
    int i;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    parse_filename(arg, filename, sizeof(filename));
    if (filename[0] == '\0' || str_icmp(filename, ".DBF") == 0) {
        printf("Syntax: COPY STRUCTURE EXTENDED TO <filename>\n");
        return;
    }

    /* Define the extended structure: 4 fields */
    memset(ext_fields, 0, sizeof(ext_fields));

    strcpy(ext_fields[0].name, "FIELD_NAME");
    ext_fields[0].type = 'C';
    ext_fields[0].length = 10;
    ext_fields[0].offset = 0;

    strcpy(ext_fields[1].name, "FIELD_TYPE");
    ext_fields[1].type = 'C';
    ext_fields[1].length = 1;
    ext_fields[1].offset = 10;

    strcpy(ext_fields[2].name, "FIELD_LEN");
    ext_fields[2].type = 'N';
    ext_fields[2].length = 3;
    ext_fields[2].decimals = 0;
    ext_fields[2].offset = 11;

    strcpy(ext_fields[3].name, "FIELD_DEC");
    ext_fields[3].type = 'N';
    ext_fields[3].length = 3;
    ext_fields[3].decimals = 0;
    ext_fields[3].offset = 14;

    if (dbf_create(filename, ext_fields, 4) < 0) {
        prog_error_fmt(ERR_CANNOT_CREATE, "Error creating %s", filename);
        return;
    }

    dbf_init(&dest);
    if (dbf_open(&dest, filename) < 0) {
        prog_error_fmt(ERR_CANNOT_OPEN, "Error opening %s", filename);
        return;
    }

    for (i = 0; i < db->field_count; i++) {
        char numbuf[8];
        dbf_append_blank(&dest);
        dbf_set_field_raw(&dest, 0, db->fields[i].name);
        numbuf[0] = db->fields[i].type;
        numbuf[1] = '\0';
        dbf_set_field_raw(&dest, 1, numbuf);
        snprintf(numbuf, sizeof(numbuf), "%d", db->fields[i].length);
        dbf_set_field_raw(&dest, 2, numbuf);
        snprintf(numbuf, sizeof(numbuf), "%d", db->fields[i].decimals);
        dbf_set_field_raw(&dest, 3, numbuf);
        dbf_flush_record(&dest);
    }

    dbf_close(&dest);
    printf("%d field(s) copied to %s.\n", db->field_count, filename);
}

/* ---- APPEND FROM DELIMITED helper ---- */

typedef struct {
    dbf_t *db;
    const char *cond_str;
    expr_ctx_t *ectx;
    dbf_t *saved_db;
    int count;
    int error;
} append_delim_ctx_t;

static int append_row_cb(int line, const csv_row_t *row, void *ud) {
    append_delim_ctx_t *ctx = (append_delim_ctx_t *)ud;
    dbf_t *db = ctx->db;
    int f, nf;
    char formatted[256];

    (void)line;

    dbf_append_blank(db);

    /* Fill fields positionally: CSV field 0 -> DB field 0, etc. */
    nf = row->num_fields;
    if (nf > db->field_count) nf = db->field_count;

    for (f = 0; f < nf; f++) {
        const char *val = row->fields[f];
        char type = db->fields[f].type;
        int flen = db->fields[f].length;
        int fdec = db->fields[f].decimals;

        if (type == 'M') continue; /* skip memo fields */

        switch (type) {
        case 'C':
            field_format_char(formatted, flen, val);
            break;
        case 'F':
        case 'N':
            field_format_numeric(formatted, flen, fdec, val);
            break;
        case 'D':
            field_format_date(formatted, val);
            break;
        case 'L':
            field_format_logical(formatted, val);
            break;
        default:
            field_format_char(formatted, flen, val);
            break;
        }
        dbf_set_field_raw(db, f, formatted);
    }

    /* Evaluate FOR condition against the destination record */
    if (ctx->cond_str) {
        value_t cond;
        ctx->ectx->db = db;
        if (expr_eval_str(ctx->ectx, ctx->cond_str, &cond) != 0) {
            report_expr_error();
            ctx->ectx->db = ctx->saved_db;
            ctx->error = 1;
            return 1; /* stop processing */
        }
        ctx->ectx->db = ctx->saved_db;
        if (cond.type != VAL_LOGIC || !cond.logic) {
            /* Condition false: undo the append */
            db->record_count--;
            db->current_record = 0;
            db->record_dirty = 0;
            dbf_write_header_counts(db);
            return 0; /* continue */
        }
    }

    dbf_flush_record(db);
    indexes_insert_current(db);
    ctx->count++;
    return 0;
}

static void cmd_append_from_delimited(dbf_t *db, const char *filename,
                                       char quote_char, int blank_mode,
                                       const char *cond_str) {
    static csv_parser_t parser; /* ~33KB — static to avoid stack overflow */
    append_delim_ctx_t ctx;
    FILE *fp;
    char buf[4096];
    int n;

    ctx.db = db;
    ctx.cond_str = cond_str;
    ctx.ectx = &expr_ctx;
    ctx.saved_db = expr_ctx.db;
    ctx.count = 0;
    ctx.error = 0;

    if (blank_mode) {
        csv_import_blank(filename, append_row_cb, &ctx);
        if (set_opts.talk) printf("%d record(s) appended.\n", ctx.count);
        return;
    }

    fp = fopen(filename, "rb");
    if (!fp) {
        file_not_found(filename);
        return;
    }

    csv_parser_init(&parser, quote_char, append_row_cb, &ctx);

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (csv_parser_feed(&parser, buf, n, 0) < 0) break;
        if (ctx.error) break;
    }

    /* Final flush on EOF */
    if (!ctx.error && !parser.error)
        csv_parser_feed(&parser, "", 0, 1);

    fclose(fp);

    if (parser.error == 1)
        printf("CSV parse error at line %d.\n", parser.line_number);

    if (set_opts.talk) printf("%d record(s) appended.\n", ctx.count);
}

/* ---- Parse filename without forcing .DBF extension ---- */
static const char *parse_filename_raw(const char *arg, char *filename, int size) {
    const char *p = skip_ws(arg);
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < size - 1)
        filename[i++] = *p++;
    filename[i] = '\0';
    path_normalize(filename);
    upper_basename(filename);
    trim_right(filename);
    return p;
}

/* ---- Check if filename has a dot extension ---- */
static int has_extension(const char *filename) {
    const char *base = strrchr(filename, '/');
    const char *dot;
    if (!base) base = filename;
    dot = strrchr(base, '.');
    return dot != NULL && dot != base;
}

/* ---- APPEND FROM [DELIMITED [WITH <char>|BLANK]] [FOR cond] ---- */
static void cmd_append_from(dbf_t *db, const char *arg) {
    char filename[64];
    const char *p;
    const char *cond_str = NULL;
    dbf_t source;
    uint32_t i;
    int count = 0;
    dbf_t *saved_db;
    int delimited = 0;
    int blank_mode = 0;
    char quote_char = '"';

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    /* Parse filename without forcing .DBF — we'll add extension later */
    p = parse_filename_raw(arg, filename, sizeof(filename));

    if (filename[0] == '\0') {
        printf("Syntax: APPEND FROM <filename> [DELIMITED [WITH <char>|BLANK]] [FOR cond]\n");
        return;
    }

    /* Check for DELIMITED keyword */
    p = skip_ws(p);
    if (str_imatch(p, "DELIMITED")) {
        delimited = 1;
        p = skip_ws(p + 9);

        /* Check for WITH clause */
        if (str_imatch(p, "WITH")) {
            p = skip_ws(p + 4);
            if (str_imatch(p, "BLANK")) {
                blank_mode = 1;
                p = skip_ws(p + 5);
            } else if (*p) {
                /* WITH <char> — custom quote character */
                quote_char = *p++;
                p = skip_ws(p);
            }
        }
    }

    /* Check for SDF keyword */
    {
        int sdf_mode = 0;
        if (str_imatch(p, "SDF")) {
            sdf_mode = 1;
            p = skip_ws(p + 3);
        } else if (str_imatch(p, "TYPE") && str_imatch(skip_ws(p + 4), "SDF")) {
            sdf_mode = 1;
            p = skip_ws(skip_ws(p + 4) + 3);
        }
        if (sdf_mode) {
            /* Parse FOR clause */
            p = skip_ws(p);
            if (str_imatch(p, "FOR"))
                cond_str = skip_ws(p + 3);

            if (!has_extension(filename)) {
                if ((int)strlen(filename) + 4 < (int)sizeof(filename))
                    strcat(filename, ".TXT");
            }

            /* Read SDF file and append records */
            {
                FILE *fp = fopen(filename, "rb");
                char line[4096];
                int lcount = 0;
                if (!fp) {
                    file_not_found(filename);
                    return;
                }

                while (fgets(line, sizeof(line), fp)) {
                    int offset = 0, f;
                    char formatted[256];

                    /* Strip trailing CR/LF */
                    {
                        int len = strlen(line);
                        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) len--;
                        line[len] = '\0';
                    }

                    if (line[0] == '\0') continue;

                    dbf_append_blank(db);

                    for (f = 0; f < db->field_count; f++) {
                        int flen = db->fields[f].length;
                        char slice[256];
                        int slen;

                        /* Extract fixed-width slice */
                        slen = strlen(line + offset);
                        if (slen > flen) slen = flen;
                        if (slen > 0) memcpy(slice, line + offset, slen);
                        if (slen < flen) memset(slice + slen, ' ', flen - slen);
                        slice[flen] = '\0';
                        offset += flen;

                        /* For date fields, convert from YYYYMMDD */
                        if (db->fields[f].type == 'D') {
                            /* SDF dates are YYYYMMDD — store directly */
                            dbf_set_field_raw(db, f, slice);
                        } else {
                            dbf_set_field_raw(db, f, slice);
                        }
                    }

                    if (cond_str) {
                        value_t cond_v;
                        if (expr_eval_str(&expr_ctx, cond_str, &cond_v) != 0) {
                            report_expr_error();
                            fclose(fp);
                            return;
                        }
                        if (cond_v.type != VAL_LOGIC || !cond_v.logic) {
                            /* Remove the appended record by going back */
                            db->record_count--;
                            db->current_record = db->record_count;
                            continue;
                        }
                    }

                    dbf_flush_record(db);
                    lcount++;
                }

                fclose(fp);
                if (set_opts.talk) printf("%d record(s) added.\n", lcount);
            }
            return;
        }
    }

    /* Parse FOR clause */
    p = skip_ws(p);
    if (str_imatch(p, "FOR")) {
        cond_str = skip_ws(p + 3);
    }

    /* Add default extension if none provided */
    if (!has_extension(filename)) {
        if (delimited) {
            if ((int)strlen(filename) + 4 < (int)sizeof(filename))
                strcat(filename, ".TXT");
        } else {
            if ((int)strlen(filename) + 4 < (int)sizeof(filename))
                strcat(filename, ".DBF");
        }
    }

    if (delimited) {
        cmd_append_from_delimited(db, filename, quote_char, blank_mode, cond_str);
        return;
    }

    /* ---- Standard DBF-to-DBF import ---- */

    dbf_init(&source);
    if (dbf_open(&source, filename) < 0) {
        file_not_found(filename);
        return;
    }

    /* FOR evaluates against source records */
    saved_db = expr_ctx.db;

    for (i = 1; i <= source.record_count; i++) {
        int f;
        char raw[256];

        dbf_read_record(&source, i);
        if (source.record_buf[0] == '*') continue;

        if (cond_str) {
            value_t cond;
            expr_ctx.db = &source;
            if (expr_eval_str(&expr_ctx, cond_str, &cond) != 0) {
                report_expr_error();
                expr_ctx.db = saved_db;
                dbf_close(&source);
                return;
            }
            expr_ctx.db = saved_db;
            if (cond.type != VAL_LOGIC || !cond.logic) continue;
        }

        dbf_append_blank(db);

        /* Copy matching fields by name */
        for (f = 0; f < source.field_count; f++) {
            int didx = dbf_find_field(db, source.fields[f].name);
            if (didx < 0) continue;
            /* Re-read source record since append_blank may have flushed db */
            dbf_read_record(&source, i);
            if (source.fields[f].type == 'M' && db->fields[didx].type == 'M')
                copy_memo_field(&source, f, db, didx);
            else {
                dbf_get_field_raw(&source, f, raw, sizeof(raw));
                dbf_set_field_raw(db, didx, raw);
            }
        }
        dbf_flush_record(db);
        indexes_insert_current(db);
        count++;
    }

    dbf_close(&source);
    if (set_opts.talk) printf("%d record(s) appended.\n", count);
}

/* ---- SORT TO file ON field [/A][/D][/C] [scope] [FOR cond] ---- */

/* ---- SORT TO file ON field [/A][/D][/C] [scope] [FOR cond] ---- */

typedef struct {
    uint32_t recno;
    char key[256];
} sort_entry_t;

typedef struct {
    int ascending;
    int case_insensitive;
    int key_type; /* 0=char/default, 1=numeric, 2=date */
} sort_ctx_t;

static int sort_compare_r(const void *a, const void *b, void *arg) {
    const sort_entry_t *sa = (const sort_entry_t *)a;
    const sort_entry_t *sb = (const sort_entry_t *)b;
    sort_ctx_t *ctx = (sort_ctx_t *)arg;
    int cmp;
    if (ctx->key_type == 0 && ctx->case_insensitive)
        cmp = str_icmp(sa->key, sb->key);
    else
        cmp = strcmp(sa->key, sb->key);
    return ctx->ascending ? cmp : -cmp;
}

#define MAX_SORT_ENTRIES 2000
#define MAX_SORT_CHUNKS 64

static int make_sort_chunk_name(char *out, size_t out_size) {
    static unsigned counter;
    unsigned attempt = 0;
    unsigned seed = (unsigned)time(NULL);
    while (attempt < 1000) {
        unsigned id = seed + counter++;
        snprintf(out, out_size, "sort_%08x.tmp", id);
        FILE *tf = fopen(out, "rb");
        if (!tf) return 1;
        fclose(tf);
        attempt++;
    }
    return 0;
}

static void cleanup_sort_chunks(char chunk_names[][64], int nchunks) {
    int j;
    for (j = 0; j < nchunks; j++) {
        remove(chunk_names[j]);
    }
}

static void cmd_sort(dbf_t *db, const char *arg) {
    char filename[64];
    char field_name[DBF_MAX_FIELD_NAME];
    const char *p;
    int field_idx;
    int nentries = 0;
    scope_t scope;
    const char *cond_str = NULL;
    uint32_t start, end, i;
    dbf_t dest;
    int count = 0;
    int f;
    sort_ctx_t ctx;
    sort_entry_t *sort_entries;
    int nchunks = 0;
    char chunk_names[MAX_SORT_CHUNKS][64];
    int chunk_overflow = 0;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    /* Parse: SORT TO <filename> ON <field> [/A][/D][/C] [scope] [FOR cond] */
    p = parse_filename(arg, filename, sizeof(filename));

    if (filename[0] == '\0' || str_icmp(filename, ".DBF") == 0) {
        printf("Syntax: SORT TO <filename> ON <field> [/A][/D][/C]\n");
        return;
    }

    p = skip_ws(p);
    if (!str_imatch(p, "ON")) {
        printf("Syntax: SORT TO <filename> ON <field> [/A][/D][/C]\n");
        return;
    }
    p = skip_ws(p + 2);

    /* Parse field name */
    {
        int fi = 0;
        while (is_ident_char(*p) && fi < DBF_MAX_FIELD_NAME - 1)
            field_name[fi++] = *p++;
        field_name[fi] = '\0';
    }
    str_upper(field_name);

    field_idx = dbf_find_field(db, field_name);
    if (field_idx < 0) {
        prog_error_fmt(ERR_SYNTAX, "Field not found: %s", field_name);
        return;
    }

    /* Parse flags */
    ctx.ascending = 1;
    ctx.case_insensitive = 0;
    ctx.key_type = 0;
    if (db->fields[field_idx].type == 'N') ctx.key_type = 1;
    else if (db->fields[field_idx].type == 'D') ctx.key_type = 2;
    p = skip_ws(p);
    while (*p == '/') {
        p++;
        char fc = *p;
        if (fc >= 'a' && fc <= 'z') fc -= 32;
        if (fc == 'A') ctx.ascending = 1;
        else if (fc == 'D') ctx.ascending = 0;
        else if (fc == 'C') ctx.case_insensitive = 1;
        p++;
        p = skip_ws(p);
    }

    /* Parse scope */
    scope = parse_scope(&p);

    /* Parse FOR */
    if (str_imatch(p, "FOR")) {
        cond_str = skip_ws(p + 3);
    }

    if (scope_bounds(db, &scope, &start, &end) < 0) {
        prog_error(ERR_RECORD_RANGE, "Invalid scope");
        return;
    }

    sort_entries = (sort_entry_t *)malloc(MAX_SORT_ENTRIES * sizeof(sort_entry_t));
    if (!sort_entries) {
        prog_error(ERR_OUT_OF_MEMORY, "Out of memory for sort buffer");
        return;
    }

    /* Phase 1: Create sorted chunks */
    for (i = start; i <= end; i++) {
        char raw[256];
        dbf_read_record(db, i);
        if (skip_deleted(db->record_buf) || !check_filter(db)) continue;

        if (cond_str) {
            value_t cond;
            if (expr_eval_str(&expr_ctx, cond_str, &cond) != 0) {
                report_expr_error();
                free(sort_entries);
                cleanup_sort_chunks(chunk_names, nchunks);
                return;
            }
            if (cond.type != VAL_LOGIC || !cond.logic) continue;
        }

        sort_entries[nentries].recno = i;
        dbf_get_field_raw(db, field_idx, raw, sizeof(raw));
        if (ctx.key_type == 1) {
            value_t v = val_num(atof(raw));
            index_format_key_value(1, &v, sort_entries[nentries].key,
                                   sizeof(sort_entries[nentries].key));
        } else if (ctx.key_type == 2) {
            value_t v = val_date(date_from_dbf(raw));
            index_format_key_value(2, &v, sort_entries[nentries].key,
                                   sizeof(sort_entries[nentries].key));
        } else {
            str_copy(sort_entries[nentries].key, raw, sizeof(sort_entries[nentries].key));
        }
        nentries++;

        if (nentries == MAX_SORT_ENTRIES) {
            /* Sort and write chunk */
            qsort_r(sort_entries, nentries, sizeof(sort_entry_t), sort_compare_r, &ctx);
            if (!make_sort_chunk_name(chunk_names[nchunks], sizeof(chunk_names[nchunks]))) {
                prog_error(ERR_FILE_IO, "Error creating sort chunk");
                chunk_overflow = 1;
                break;
            }
            FILE *tf = fopen(chunk_names[nchunks], "wb");
            if (tf) {
                fwrite(sort_entries, sizeof(sort_entry_t), nentries, tf);
                fclose(tf);
                nchunks++;
                nentries = 0;
                if (nchunks >= MAX_SORT_CHUNKS) {
                    prog_error(ERR_FILE_IO, "Too many sort chunks");
                    chunk_overflow = 1;
                    break;
                }
            } else {
                prog_error(ERR_FILE_IO, "Error creating sort chunk");
                chunk_overflow = 1;
                break;
            }
        }
    }
    if (chunk_overflow) {
        free(sort_entries);
        cleanup_sort_chunks(chunk_names, nchunks);
        return;
    }

    /* Final chunk */
    if (nentries > 0) {
        qsort_r(sort_entries, nentries, sizeof(sort_entry_t), sort_compare_r, &ctx);
        if (nchunks == 0) {
            /* All fit in memory, proceed directly to write phase below */
        } else {
            if (!make_sort_chunk_name(chunk_names[nchunks], sizeof(chunk_names[nchunks]))) {
                prog_error(ERR_FILE_IO, "Error creating sort chunk");
                free(sort_entries);
                cleanup_sort_chunks(chunk_names, nchunks);
                return;
            }
            FILE *tf = fopen(chunk_names[nchunks], "wb");
            if (!tf) {
                prog_error(ERR_FILE_IO, "Error creating sort chunk");
                free(sort_entries);
                cleanup_sort_chunks(chunk_names, nchunks);
                return;
            }
            fwrite(sort_entries, sizeof(sort_entry_t), nentries, tf);
            fclose(tf);
            nchunks++;
            nentries = 0;
        }
    }

    if (nchunks == 0 && nentries == 0) {
        prog_error(ERR_RECORD_RANGE, "No records to sort");
        free(sort_entries);
        return;
    }

    /* Phase 2: Create destination file */
    {
        dbf_field_t fields[DBF_MAX_FIELDS];
        uint16_t off = 0;
        for (f = 0; f < db->field_count; f++) {
            fields[f] = db->fields[f];
            fields[f].offset = off;
            off += fields[f].length;
        }
        if (dbf_create(filename, fields, db->field_count) < 0) {
            prog_error_fmt(ERR_CANNOT_CREATE, "Error creating %s", filename);
            free(sort_entries);
            cleanup_sort_chunks(chunk_names, nchunks);
            return;
        }

        /* Create .DBT if any memo fields */
        for (f = 0; f < db->field_count; f++) {
            if (fields[f].type == 'M') {
                dbf_memo_create(filename);
                break;
            }
        }
    }

    dbf_init(&dest);
    if (dbf_open(&dest, filename) < 0) {
        prog_error_fmt(ERR_CANNOT_OPEN, "Error opening %s", filename);
        free(sort_entries);
        cleanup_sort_chunks(chunk_names, nchunks);
        return;
    }

    if (nchunks == 0) {
        /* In-memory sort result */
        for (i = 0; i < (uint32_t)nentries; i++) {
            char raw[256];
            dbf_read_record(db, sort_entries[i].recno);
            dbf_append_blank(&dest);
            for (f = 0; f < db->field_count; f++) {
                if (db->fields[f].type == 'M')
                    copy_memo_field(db, f, &dest, f);
                else {
                    dbf_get_field_raw(db, f, raw, sizeof(raw));
                    dbf_set_field_raw(&dest, f, raw);
                }
            }
            dbf_flush_record(&dest);
            count++;
        }
    } else {
        /* Phase 3: K-way Merge */
        FILE *tfs[MAX_SORT_CHUNKS];
        sort_entry_t current[MAX_SORT_CHUNKS];
        int active[MAX_SORT_CHUNKS];
        int active_count = 0;

        for (int j = 0; j < nchunks; j++) {
            tfs[j] = fopen(chunk_names[j], "rb");
            if (tfs[j] && fread(&current[j], sizeof(sort_entry_t), 1, tfs[j]) == 1) {
                active[j] = 1;
                active_count++;
            } else {
                active[j] = 0;
                if (tfs[j]) fclose(tfs[j]);
            }
        }

        while (active_count > 0) {
            int best = -1;
            for (int j = 0; j < nchunks; j++) {
                if (!active[j]) continue;
                if (best == -1 || sort_compare_r(&current[j], &current[best], &ctx) < 0) {
                    best = j;
                }
            }

            if (best != -1) {
                /* Write best record to dest */
                char raw[256];
                dbf_read_record(db, current[best].recno);
                dbf_append_blank(&dest);
                for (f = 0; f < db->field_count; f++) {
                    if (db->fields[f].type == 'M')
                        copy_memo_field(db, f, &dest, f);
                    else {
                        dbf_get_field_raw(db, f, raw, sizeof(raw));
                        dbf_set_field_raw(&dest, f, raw);
                    }
                }
                dbf_flush_record(&dest);
                count++;

                /* Read next from that chunk */
                if (fread(&current[best], sizeof(sort_entry_t), 1, tfs[best]) != 1) {
                    active[best] = 0;
                    active_count--;
                    fclose(tfs[best]);
                    remove(chunk_names[best]);
                }
            }
        }
        /* Cleanup any unopened/empty chunks */
        cleanup_sort_chunks(chunk_names, nchunks);
    }

    dbf_close(&dest);
    free(sort_entries);
    if (set_opts.talk) printf("%d record(s) sorted.\n", count);
}

/* ---- ERASE <file> ---- */
static void cmd_erase(const char *arg) {
    char filename[64];
    const char *p = skip_ws(arg);
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < 63)
        filename[i++] = *p++;
    filename[i] = '\0';
    if (filename[0] == '\0') {
        printf("Syntax: ERASE <filename>\n");
        return;
    }
    path_normalize(filename);
    if (remove(filename) != 0) {
        file_not_found(filename);
    } else {
        printf("File erased.\n");
    }
}

/* ---- RENAME <old> TO <new> ---- */
static void cmd_rename(const char *arg) {
    char oldname[64], newname[64];
    const char *p = skip_ws(arg);
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < 63)
        oldname[i++] = *p++;
    oldname[i] = '\0';
    path_normalize(oldname);
    p = skip_ws(p);
    if (!str_imatch(p, "TO")) {
        printf("Syntax: RENAME <oldfile> TO <newfile>\n");
        return;
    }
    p = skip_ws(p + 2);
    i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < 63)
        newname[i++] = *p++;
    newname[i] = '\0';
    path_normalize(newname);
    if (oldname[0] == '\0' || newname[0] == '\0') {
        printf("Syntax: RENAME <oldfile> TO <newfile>\n");
        return;
    }
    /* Try native rename first, fall back to copy+delete */
    if (rename(oldname, newname) != 0) {
        FILE *fin = fopen(oldname, "rb");
        if (!fin) {
            file_not_found(oldname);
            return;
        }
        {
            FILE *fout = fopen(newname, "wb");
            char buf[512];
            int n;
            if (!fout) {
                fclose(fin);
                prog_error_fmt(ERR_CANNOT_CREATE, "Cannot create: %s", newname);
                return;
            }
            while ((n = fread(buf, 1, sizeof(buf), fin)) > 0)
                fwrite(buf, 1, n, fout);
            fclose(fin);
            fclose(fout);
            remove(oldname);
        }
        printf("File renamed.\n");
    } else {
        printf("File renamed.\n");
    }
}

/* ---- COPY FILE <src> TO <dest> ---- */
static void cmd_copy_file(const char *arg) {
    char src[64], dst[64];
    const char *p = skip_ws(arg);
    int i = 0;
    FILE *fin, *fout;
    char buf[512];
    int n;

    while (*p && *p != ' ' && *p != '\t' && i < 63)
        src[i++] = *p++;
    src[i] = '\0';
    path_normalize(src);
    p = skip_ws(p);
    if (!str_imatch(p, "TO")) {
        printf("Syntax: COPY FILE <source> TO <destination>\n");
        return;
    }
    p = skip_ws(p + 2);
    i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < 63)
        dst[i++] = *p++;
    dst[i] = '\0';
    path_normalize(dst);
    if (src[0] == '\0' || dst[0] == '\0') {
        printf("Syntax: COPY FILE <source> TO <destination>\n");
        return;
    }
    fin = fopen(src, "rb");
    if (!fin) {
        file_not_found(src);
        return;
    }
    fout = fopen(dst, "wb");
    if (!fout) {
        fclose(fin);
        prog_error_fmt(ERR_CANNOT_CREATE, "Cannot create: %s", dst);
        return;
    }
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0)
        fwrite(buf, 1, n, fout);
    fclose(fin);
    fclose(fout);
    printf("File copied.\n");
}

/* ---- INDEX [UNIQUE] ON <expr> TO <file> [UNIQUE] ---- */
static void cmd_index_on(dbf_t *db, lexer_t *l) {
    char key_expr[256];
    char filename[64];
    int is_unique = set_opts.unique;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    if (cmd_kw(l, "UNIQUE")) {
        is_unique = 1;
        lex_next(l);
    }

    if (!cmd_kw(l, "ON")) {
        printf("Syntax: INDEX [UNIQUE] ON <expr> TO <filename> [UNIQUE]\n");
        return;
    }
    lex_next(l); /* skip ON */

    /* Find TO keyword or end */
    const char *expr_start = l->token_start;
    while (l->current.type != TOK_EOF && !cmd_kw(l, "TO"))
        lex_next(l);

    if (l->current.type == TOK_EOF) {
        printf("Syntax: INDEX [UNIQUE] ON <expr> TO <filename> [UNIQUE]\n");
        return;
    }

    {
        int len = (int)(l->token_start - expr_start);
        if (len > (int)sizeof(key_expr) - 1) len = (int)sizeof(key_expr) - 1;
        memcpy(key_expr, expr_start, len);
        key_expr[len] = '\0';
        trim_right(key_expr);
    }

    if (!cmd_kw(l, "TO")) {
        printf("Syntax: INDEX [UNIQUE] ON <expr> TO <filename> [UNIQUE]\n");
        return;
    }
    lex_next(l); /* skip TO */

    if (consume_filename(l, filename, sizeof(filename)) < 0) {
        printf("Syntax: INDEX [UNIQUE] ON <expr> TO <filename> [UNIQUE]\n");
        return;
    }

    if (cmd_kw(l, "UNIQUE")) {
        is_unique = 1;
        lex_next(l);
    }

    /* Parse optional FOR condition */
    char for_cond[256];
    int is_descending = 0;
    for_cond[0] = '\0';

    if (cmd_kw(l, "FOR")) {
        lex_next(l); /* skip FOR */
        const char *for_start = l->token_start;
        while (l->current.type != TOK_EOF && !cmd_kw(l, "DESCENDING") && !cmd_kw(l, "ASCENDING"))
            lex_next(l);
        int flen = (int)(l->token_start - for_start);
        if (flen > (int)sizeof(for_cond) - 1) flen = (int)sizeof(for_cond) - 1;
        memcpy(for_cond, for_start, flen);
        for_cond[flen] = '\0';
        trim_right(for_cond);
    }

    if (cmd_kw(l, "DESCENDING")) {
        is_descending = 1;
        lex_next(l);
    } else if (cmd_kw(l, "ASCENDING")) {
        is_descending = 0;
        lex_next(l);
    }

    ensure_ndx_ext(filename, sizeof(filename));

    if (filename[0] == '\0' || str_icmp(filename, ".NDX") == 0) {
        printf("Syntax: INDEX [UNIQUE] ON <expr> TO <filename> [UNIQUE]\n");
        return;
    }

    /* Close all existing indexes and build fresh */
    close_all_indexes(cur_wa());

    /* Build the index in slot 0 */
    cur_wa()->indexes[0].unique = is_unique;
    if (index_build(&cur_wa()->indexes[0], db, &expr_ctx, key_expr, filename, for_cond, is_descending) < 0) {
        prog_error(ERR_FILE_IO, "Error building index");
        return;
    }

    /* Write to file */
    if (index_write(&cur_wa()->indexes[0]) < 0) {
        prog_error(ERR_FILE_IO, "Error writing index file");
        return;
    }

    cur_wa()->num_indexes = 1;
    cur_wa()->order = 1;

    /* Position to first indexed record */
    if (cur_wa()->indexes[0].nentries > 0) {
        uint32_t rec;
        index_top(&cur_wa()->indexes[0]);
        rec = index_current_recno(&cur_wa()->indexes[0]);
        if (rec > 0) dbf_read_record(db, rec);
        expr_ctx.eof_flag = 0;
        expr_ctx.bof_flag = 0;
    }

    if (set_opts.talk) printf("%d record(s) indexed.\n", cur_wa()->indexes[0].nentries);
}

/* ---- SET INDEX TO [file1 [, file2 ...]] ---- */
static void cmd_set_index(dbf_t *db, const char *arg) {
    const char *p = skip_ws(arg);

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    /* Close all existing indexes */
    close_all_indexes(cur_wa());

    if (*p == '\0') {
        /* SET INDEX TO with no args = close all */
        printf("Index closed.\n");
        return;
    }

    /* Parse comma-separated index files */
    while (*p && cur_wa()->num_indexes < MAX_INDEXES) {
        char filename[64];
        int i = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != ',' && i < 63)
            filename[i++] = *p++;
        filename[i] = '\0';

        if (filename[0]) {
            int slot = cur_wa()->num_indexes;
            path_normalize(filename);
            ensure_ndx_ext(filename, sizeof(filename));
            {
                char resolved[320];
                if (cmd_resolve_file(filename, resolved, sizeof(resolved)) == 0)
                    str_copy(filename, resolved, sizeof(filename));
            }
            if (index_read(&cur_wa()->indexes[slot], filename) == 0) {
                index_compile_key_ast(&cur_wa()->indexes[slot], &memvar_store);
                cur_wa()->num_indexes++;
                printf("Index %s opened (%d entries).\n", filename,
                       cur_wa()->indexes[slot].nentries);
            } else {
                prog_error_fmt(ERR_CANNOT_OPEN, "Cannot open index: %s", filename);
            }
        }

        p = skip_ws(p);
        if (*p == ',') { p++; p = skip_ws(p); }
        else break;
    }

    /* Set controlling index to first one */
    if (cur_wa()->num_indexes > 0) {
        index_t *idx;
        uint32_t rec;
        cur_wa()->order = 1;

        /* Position to first indexed record */
        idx = controlling_index();
        if (idx && idx->nentries > 0) {
            index_top(idx);
            rec = index_current_recno(idx);
            if (rec > 0) dbf_read_record(db, rec);
            expr_ctx.eof_flag = 0;
            expr_ctx.bof_flag = 0;
        }
    }
}

/* ---- SEEK <expr> ---- */
static void cmd_seek(dbf_t *db, const char *arg) {
    const char *p = skip_ws(arg);
    value_t val;
    char key[MAX_INDEX_KEY + 1];
    index_t *idx;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    idx = controlling_index();
    if (!idx) {
        printf("No index active.\n");
        return;
    }

    if (expr_eval_str(&expr_ctx, p, &val) != 0) {
        report_expr_error();
        return;
    }

    index_format_key_value(idx->key_type, &val, key, sizeof(key));
    if (idx->key_type == 0)
        trim_right(key);

    {
    /* For string keys with SET EXACT OFF, use prefix matching */
    int match_len = 0;
    if (idx->key_type == 0 && !set_opts.exact)
        match_len = strlen(key);

    if (index_seek(idx, key, match_len)) {
        uint32_t rec = index_current_recno(idx);
        expr_ctx.found = 1;
        if (rec > 0) {
            dbf_read_record(db, rec);
            expr_ctx.eof_flag = 0;
            expr_ctx.bof_flag = 0;
        }
    } else {
        expr_ctx.found = 0;
        {
            uint32_t rec = index_current_recno(idx);
            if (rec > 0) {
                dbf_read_record(db, rec);
                expr_ctx.eof_flag = 0;
            } else {
                expr_ctx.eof_flag = 1;
            }
        }
        if (set_opts.talk && !set_opts.softseek) printf("Not found.\n");
    }
    } /* match_len scope */
}

/* ---- FIND <literal> ---- */
static void cmd_find(dbf_t *db, const char *arg) {
    const char *p = skip_ws(arg);
    char key[MAX_INDEX_KEY + 1];
    index_t *idx;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    idx = controlling_index();
    if (!idx) {
        printf("No index active.\n");
        return;
    }

    str_copy(key, p, sizeof(key));
    trim_right(key);

    {
    int match_len = (!set_opts.exact && idx->key_type == 0) ? (int)strlen(key) : 0;
    if (index_seek(idx, key, match_len)) {
        uint32_t rec = index_current_recno(idx);
        expr_ctx.found = 1;
        if (rec > 0) {
            dbf_read_record(db, rec);
            expr_ctx.eof_flag = 0;
            expr_ctx.bof_flag = 0;
        }
    } else {
        expr_ctx.found = 0;
        {
            uint32_t rec = index_current_recno(idx);
            if (rec > 0) {
                dbf_read_record(db, rec);
                expr_ctx.eof_flag = 0;
            } else {
                expr_ctx.eof_flag = 1;
            }
        }
        if (set_opts.talk) printf("Not found.\n");
    }
    } /* match_len scope */
}

/* ---- REINDEX ---- */
static void cmd_reindex(dbf_t *db) {
    work_area_t *wa = cur_wa();
    int i, total = 0;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    if (wa->num_indexes == 0) {
        printf("No index active.\n");
        return;
    }

    for (i = 0; i < wa->num_indexes; i++) {
        index_t *idx = &wa->indexes[i];
        char key_expr[256];
        char filename[64];
        str_copy(key_expr, idx->key_expr, sizeof(key_expr));
        str_copy(filename, idx->filename, sizeof(filename));

        if (index_build(idx, db, &expr_ctx, key_expr, filename,
                        idx->for_expr, idx->descending) < 0) {
            prog_error_fmt(ERR_FILE_IO, "Error rebuilding index %s", filename);
            return;
        }

        if (index_write(idx) < 0) {
            prog_error_fmt(ERR_FILE_IO, "Error writing index file %s", filename);
            return;
        }
        total += idx->nentries;
    }

    /* Position to first entry in controlling index */
    {
        index_t *ctrl = controlling_index();
        if (ctrl && ctrl->nentries > 0) {
            uint32_t rec;
            index_top(ctrl);
            rec = index_current_recno(ctrl);
            if (rec > 0) dbf_read_record(db, rec);
            expr_ctx.eof_flag = 0;
            expr_ctx.bof_flag = 0;
        }
    }

    if (set_opts.talk) printf("%d record(s) reindexed.\n", total);
}

/* ---- Close all work areas ---- */
void cmd_close_all(void) {
    int i;
    for (i = 0; i < MAX_AREAS; i++) {
        work_area_t *wa = area_get(i);
        if (dbf_is_open(&wa->db))
            dbf_close(&wa->db);
        close_all_indexes(wa);
        wa->alias[0] = '\0';
        wa->locate_cond[0] = '\0';
        wa->locate_last_rec = 0;
        wa->filter_cond[0] = '\0';
        if (wa->filter_ast) { ast_free(wa->filter_ast); wa->filter_ast = NULL; }
        wa->relation_expr[0] = '\0';
        wa->relation_target = -1;
    }
    area_set_current_idx(0);
    ll_close_all();
}

/* ---- Helper: ensure filename has .FRM extension ---- */
static void ensure_frm_ext(char *filename, int size) {
    str_upper(filename);
    trim_right(filename);
    if (strlen(filename) < 5 || str_icmp(filename + strlen(filename) - 4, ".FRM") != 0) {
        if ((int)strlen(filename) + 4 < size)
            strcat(filename, ".FRM");
    }
}

/* ---- Helper: ensure filename has .LBL extension ---- */
static void ensure_lbl_ext(char *filename, int size) {
    str_upper(filename);
    trim_right(filename);
    if (strlen(filename) < 5 || str_icmp(filename + strlen(filename) - 4, ".LBL") != 0) {
        if ((int)strlen(filename) + 4 < size)
            strcat(filename, ".LBL");
    }
}

/* ---- CREATE REPORT ---- */
static void cmd_create_report(const char *arg) {
    char filename[64];
    char line[256];
    frm_def_t def;

    arg = skip_ws(arg);
    if (*arg == '\0') {
        printf("Syntax: CREATE REPORT <filename>\n");
        return;
    }

    str_copy(filename, arg, sizeof(filename));
    ensure_frm_ext(filename, sizeof(filename));

    frm_init(&def);

    /* Page title */
    printf("Page title? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    str_copy(def.title, line, sizeof(def.title));

    /* Page width */
    printf("Page width (80)? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    if (line[0]) def.page_width = atoi(line);

    /* Left margin */
    printf("Left margin (8)? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    if (line[0]) def.left_margin = atoi(line);

    /* Right margin */
    printf("Right margin (0)? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    if (line[0]) def.right_margin = atoi(line);

    /* Lines per page */
    printf("Lines per page (58)? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    if (line[0]) def.lines_per_page = atoi(line);

    /* Double space */
    printf("Double space report? (Y/N) ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    str_upper(line);
    def.double_space = (line[0] == 'Y') ? 1 : 0;

    /* Group on */
    printf("Group on expression? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    str_copy(def.group_expr, line, sizeof(def.group_expr));

    if (def.group_expr[0]) {
        printf("Group heading? ");
        if (read_line(line, sizeof(line)) < 0) return;
        trim_right(line);
        str_copy(def.group_header, line, sizeof(def.group_header));
    }

    /* Sub-group on */
    printf("Sub-group on expression? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    str_copy(def.subgroup_expr, line, sizeof(def.subgroup_expr));

    if (def.subgroup_expr[0]) {
        printf("Sub-group heading? ");
        if (read_line(line, sizeof(line)) < 0) return;
        trim_right(line);
        str_copy(def.subgroup_header, line, sizeof(def.subgroup_header));
    }

    /* Summary only */
    printf("Summary report only? (Y/N) ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    str_upper(line);
    def.summary_only = (line[0] == 'Y') ? 1 : 0;

    /* Eject after */
    printf("Page eject after report? (Y/N) ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    str_upper(line);
    def.eject_after = (line[0] == 'Y') ? 1 : 0;

    /* Columns */
    while (def.num_columns < FRM_MAX_COLUMNS) {
        int col = def.num_columns;

        printf("Col %2d:\n", col + 1);
        printf("  Contents? ");
        if (read_line(line, sizeof(line)) < 0) break;
        trim_right(line);
        if (line[0] == '\0') break;
        str_copy(def.columns[col].content, line, sizeof(def.columns[col].content));

        printf("  Heading? ");
        if (read_line(line, sizeof(line)) < 0) break;
        trim_right(line);
        str_copy(def.columns[col].header, line, sizeof(def.columns[col].header));

        printf("  Width? ");
        if (read_line(line, sizeof(line)) < 0) break;
        trim_right(line);
        def.columns[col].width = atoi(line);
        if (def.columns[col].width < 1) def.columns[col].width = 10;

        printf("  Decimal places? ");
        if (read_line(line, sizeof(line)) < 0) break;
        trim_right(line);
        def.columns[col].decimals = atoi(line);

        if (def.columns[col].decimals > 0) {
            printf("  Totals? (Y/N) ");
            if (read_line(line, sizeof(line)) < 0) break;
            trim_right(line);
            str_upper(line);
            def.columns[col].totals = (line[0] == 'Y') ? 1 : 0;
        }

        def.num_columns++;
    }

    if (def.num_columns == 0) {
        printf("No columns defined.\n");
        return;
    }

    if (frm_write(filename, &def) < 0) {
        prog_error_fmt(ERR_CANNOT_CREATE, "Error creating %s", filename);
        return;
    }
    printf("Report definition %s created.\n", filename);
}

static int report_kw_boundary(const char *p, const char *kw) {
    int n = (int)strlen(kw);
    if (!str_imatch(p, kw)) return 0;
    if (p[n] == '\0' || p[n] == ' ' || p[n] == '\t') return 1;
    return 0;
}

static char *report_find_next_kw(char *p) {
    char *s = p;
    while (*s) {
        if (s == p || s[-1] == ' ' || s[-1] == '\t') {
            if (report_kw_boundary(s, "FOR") ||
                report_kw_boundary(s, "HEADING") ||
                report_kw_boundary(s, "PLAIN") ||
                report_kw_boundary(s, "SUMMARY") ||
                report_kw_boundary(s, "NOEJECT") ||
                report_kw_boundary(s, "TO")) {
                return s;
            }
        }
        s++;
    }
    return NULL;
}

/* ---- REPORT FORM ---- */
static void cmd_report_form(dbf_t *db, lexer_t *l) {
    char filename[64];
    FILE *outfile = NULL;
    frm_def_t *def;
    clause_t c;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    if (l->current.type != TOK_IDENT) {
        printf("Syntax: REPORT FORM <filename> [scope] [FOR <cond>] [WHILE <cond>] [HEADING <expr>] [PLAIN] [SUMMARY] [NOEJECT] [TO PRINT] [TO FILE <file>]\n");
        return;
    }

    str_copy(filename, l->current.text, sizeof(filename));
    ensure_frm_ext(filename, sizeof(filename));
    lex_next(l);

    clause_init(&c);
    if (parse_clauses(l, &c) < 0) return;

    if (c.to_file[0]) {
        outfile = fopen(c.to_file, "w");
        if (!outfile) {
            prog_error_fmt(ERR_CANNOT_CREATE, "Cannot create %s", c.to_file);
            return;
        }
    }

    def = malloc(sizeof(frm_def_t));
    if (!def) {
        prog_error(ERR_OUT_OF_MEMORY, "Out of memory");
        if (outfile) fclose(outfile);
        return;
    }

    /* Read the .FRM file */
    if (frm_read(filename, def) < 0) {
        file_not_found(filename);
        free(def);
        if (outfile) fclose(outfile);
        return;
    }

    /* Determine output destination */
    if (!outfile)
        outfile = stdout;

    ctx_setup();
    /* Note: currently report_generate ignores WHILE/scope, passing them would require signature change */
    report_generate(def, db, &expr_ctx, c.for_cond[0] ? c.for_cond : NULL,
                    c.heading[0] ? c.heading : NULL,
                    c.plain, c.summary, c.noeject, outfile);

    free(def);
    if (outfile != stdout)
        fclose(outfile);
}

/* ---- CREATE LABEL ---- */
static void cmd_create_label(const char *arg) {
    char filename[64];
    char line[256];
    lbl_def_t def;
    int i;

    arg = skip_ws(arg);
    if (*arg == '\0') {
        printf("Syntax: CREATE LABEL <filename>\n");
        return;
    }

    str_copy(filename, arg, sizeof(filename));
    ensure_lbl_ext(filename, sizeof(filename));

    lbl_init(&def);

    /* Remark */
    printf("Label remark? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    str_copy(def.remark, line, sizeof(def.remark));

    /* Height */
    printf("Label height (5)? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    if (line[0]) def.height = atoi(line);
    if (def.height < 1) def.height = 1;
    if (def.height > LBL_MAX_LINES) def.height = LBL_MAX_LINES;

    /* Width */
    printf("Label width (35)? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    if (line[0]) def.width = atoi(line);
    if (def.width < 1) def.width = 1;

    /* Left margin */
    printf("Left margin (0)? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    if (line[0]) def.left_margin = atoi(line);

    /* Lines between */
    printf("Lines between labels (1)? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    if (line[0]) def.lines_between = atoi(line);

    /* Spaces between */
    printf("Spaces between labels (0)? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    if (line[0]) def.spaces_between = atoi(line);

    /* Labels across */
    printf("Labels across (1)? ");
    if (read_line(line, sizeof(line)) < 0) return;
    trim_right(line);
    if (line[0]) def.across = atoi(line);
    if (def.across < 1) def.across = 1;

    /* Content lines */
    def.num_lines = 0;
    for (i = 0; i < def.height; i++) {
        printf("Line %d contents? ", i + 1);
        if (read_line(line, sizeof(line)) < 0) break;
        trim_right(line);
        if (line[0] == '\0') break;
        str_copy(def.lines[i], line, LBL_EXPR_SIZE + 1);
        def.num_lines++;
    }

    if (def.num_lines == 0) {
        printf("No label lines defined.\n");
        return;
    }

    if (lbl_write(filename, &def) < 0) {
        prog_error_fmt(ERR_CANNOT_CREATE, "Error creating %s", filename);
        return;
    }
    printf("Label definition %s created.\n", filename);
}

/* ---- LABEL FORM ---- */
static void cmd_label_form(dbf_t *db, const char *arg) {
    char filename[64];
    const char *p;
    const char *for_cond = NULL;
    int sample = 0;
    FILE *outfile = NULL;
    lbl_def_t def;
    int i;

    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }

    p = skip_ws(arg);
    /* Parse filename */
    i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < 63)
        filename[i++] = *p++;
    filename[i] = '\0';
    ensure_lbl_ext(filename, sizeof(filename));

    /* Parse optional clauses */
    while (*p) {
        p = skip_ws(p);
        if (*p == '\0') break;

        if (str_imatch(p, "FOR")) {
            for_cond = skip_ws(p + 3);
            break;  /* FOR consumes rest of line */
        }
        if (str_imatch(p, "SAMPLE")) {
            sample = 1;
            p += 6;
            continue;
        }
        if (str_imatch(p, "TO")) {
            p = skip_ws(p + 2);
            if (str_imatch(p, "PRINT")) {
                p += 5;
                continue;
            }
            if (str_imatch(p, "FILE")) {
                char fname[128];
                int fi = 0;
                p = skip_ws(p + 4);
                while (*p && *p != ' ' && fi < 127) fname[fi++] = *p++;
                fname[fi] = '\0';
                outfile = fopen(fname, "w");
                if (!outfile) {
                    prog_error_fmt(ERR_CANNOT_CREATE, "Cannot create %s", fname);
                    return;
                }
                continue;
            }
        }
        /* Skip unrecognized word */
        while (*p && *p != ' ' && *p != '\t') p++;
    }

    /* Read the .LBL file */
    if (lbl_read(filename, &def) < 0) {
        file_not_found(filename);
        if (outfile) fclose(outfile);
        return;
    }

    if (!outfile)
        outfile = stdout;

    ctx_setup();
    label_generate(&def, db, &expr_ctx, for_cond, sample, outfile);

    if (outfile != stdout)
        fclose(outfile);
}

/* ---- Accessors for program.c / screen.c ---- */
memvar_store_t *cmd_get_memvar_store(void) {
    return &memvar_store;
}

expr_ctx_t *cmd_get_expr_ctx(void) {
    ctx_setup();
    return &expr_ctx;
}

int cmd_get_device(void) {
    return set_opts.device;
}

int cmd_get_console(void) {
    return set_opts.console;
}

int cmd_get_talk(void) {
    return set_opts.talk;
}

int cmd_get_confirm(void) {
    return set_opts.confirm;
}

int cmd_get_century(void) {
    return set_opts.century;
}

int cmd_get_escape(void) {
    return set_opts.escape;
}

int cmd_get_echo(void) {
    return set_opts.echo;
}

int cmd_get_margin(void) {
    return set_opts.margin;
}

int cmd_get_message_row(void) {
    return set_opts.message_row;
}

int cmd_get_wrap(void) {
    return set_opts.wrap;
}

date_format_t cmd_get_date_format(void) {
    return set_opts.date_format;
}

int cmd_get_softseek(void) {
    return set_opts.softseek;
}

int cmd_get_unique(void) {
    return set_opts.unique;
}

int cmd_get_memowidth(void) {
    return set_opts.memowidth;
}

int cmd_get_epoch(void) {
    return set_opts.epoch;
}

char cmd_get_mark(void) {
    return set_opts.mark;
}

FILE *cmd_get_alternate_fp(void) {
    return set_opts.alternate_fp;
}

int cmd_get_alternate_on(void) {
    return set_opts.alternate_on;
}

const set_options_t *cmd_get_set_opts(void) {
    return &set_opts;
}

const char *cmd_get_path(void) {
    return set_opts.path;
}

void cmd_output(const char *s) {
    printf("%s", s);
    if (set_opts.alternate_on && set_opts.alternate_fp)
        fprintf(set_opts.alternate_fp, "%s", s);
}

void cmd_output_fmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (set_opts.alternate_on && set_opts.alternate_fp) {
        va_start(ap, fmt);
        vfprintf(set_opts.alternate_fp, fmt, ap);
        va_end(ap);
    }
}

int cmd_resolve_file(const char *name, char *resolved, int size) {
    FILE *fp;

    /* Try name as-is */
    fp = fopen(name, "rb");
    if (fp) { fclose(fp); str_copy(resolved, name, size); return 0; }

    /* Try each directory in SET PATH */
    if (set_opts.path[0]) {
        char path_copy[256];
        char *dir, *save;
        str_copy(path_copy, set_opts.path, sizeof(path_copy));
        dir = strtok_r(path_copy, ";,", &save);
        while (dir) {
            char trial[320];
            dir = skip_ws(dir);
            trim_right(dir);
            if (dir[0]) {
                snprintf(trial, sizeof(trial), "%s/%s", dir, name);
                fp = fopen(trial, "rb");
                if (fp) { fclose(fp); str_copy(resolved, trial, size); return 0; }
            }
            dir = strtok_r(NULL, ";,", &save);
        }
    }

    /* Not found - just copy original */
    str_copy(resolved, name, size);
    return -1;
}

/* ---- Dispatch ---- */
/* ---- Command Dispatch Table ---- */

typedef void (*cmd_func_t)(dbf_t *db, lexer_t *l);

typedef struct {
    const char *name;
    cmd_func_t func;
} cmd_entry_t;

/* Wrappers for legacy or complex-parsing commands */
static void h_do(dbf_t *db, lexer_t *l) { (void)db; char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); prog_do(arg); }
static void h_if(dbf_t *db, lexer_t *l) { (void)db; if (prog_is_running()) { char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); prog_if(arg); } else printf("IF not allowed in interactive mode.\n"); }
static void h_else(dbf_t *db, lexer_t *l) { (void)db; (void)l; if (prog_is_running()) prog_else(); else printf("ELSE without IF.\n"); }
static void h_endif(dbf_t *db, lexer_t *l) { (void)db; (void)l; if (prog_is_running()) prog_endif(); else printf("ENDIF without IF.\n"); }
static void h_enddo(dbf_t *db, lexer_t *l) { (void)db; (void)l; if (prog_is_running()) prog_enddo(); else printf("ENDDO without DO WHILE.\n"); }
static void h_loop(dbf_t *db, lexer_t *l) { (void)db; (void)l; if (prog_is_running()) prog_loop(); else printf("LOOP without DO WHILE.\n"); }
static void h_scan(dbf_t *db, lexer_t *l) { (void)db; if (prog_is_running()) { char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); prog_scan(arg); } else printf("SCAN not allowed in interactive mode.\n"); }
static void h_endscan(dbf_t *db, lexer_t *l) { (void)db; (void)l; if (prog_is_running()) prog_endscan(); else printf("ENDSCAN without SCAN.\n"); }
static void h_case(dbf_t *db, lexer_t *l) { (void)db; if (prog_is_running()) { char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); prog_case(arg); } else printf("CASE without DO CASE.\n"); }
static void h_otherwise(dbf_t *db, lexer_t *l) { (void)db; (void)l; if (prog_is_running()) prog_otherwise(); else printf("OTHERWISE without DO CASE.\n"); }
static void h_endcase(dbf_t *db, lexer_t *l) { (void)db; (void)l; if (prog_is_running()) prog_endcase(); else printf("ENDCASE without DO CASE.\n"); }
static void h_declare(dbf_t *db, lexer_t *l) {
    (void)db;
    lex_next(l); /* skip DECLARE */
    while (l->current.type == TOK_IDENT) {
        char name[MEMVAR_NAMELEN];
        int rows = 0, cols = 1;
        str_copy(name, l->current.text, sizeof(name));
        lex_next(l);
        if (l->current.type == TOK_LPAREN || l->current.type == TOK_LBRACKET) {
            token_type_t end_type = (l->current.type == TOK_LPAREN) ? TOK_RPAREN : TOK_RBRACKET;
            lex_next(l);
            if (l->current.type == TOK_NUMBER) {
                rows = (int)l->current.num_val;
                lex_next(l);
                if (l->current.type == TOK_COMMA) {
                    lex_next(l);
                    if (l->current.type == TOK_NUMBER) {
                        cols = (int)l->current.num_val;
                        lex_next(l);
                    }
                }
            }
            if (l->current.type == end_type) {
                lex_next(l);
            }
        }
        if (rows > 0) {
            memvar_declare_array(&memvar_store, name, rows, cols);
        } else {
            prog_error_fmt(ERR_SYNTAX, "Invalid array dimensions for %s", name);
        }
        if (l->current.type == TOK_COMMA) lex_next(l);
        else break;
    }
}
static void h_return(dbf_t *db, lexer_t *l) { (void)db; if (prog_is_running()) { char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); prog_return(arg); } else printf("RETURN not in program.\n"); }
static void h_procedure(dbf_t *db, lexer_t *l) { (void)db; if (prog_is_running()) { lex_next(l); prog_procedure(l); } else printf("PROCEDURE not allowed in interactive mode.\n"); }
static void h_parameters(dbf_t *db, lexer_t *l) { (void)db; if (prog_is_running()) { lex_next(l); prog_parameters(l); } else printf("PARAMETERS not allowed in interactive mode.\n"); }
static void h_private(dbf_t *db, lexer_t *l) { (void)db; lex_next(l); prog_private(l); }
static void h_public(dbf_t *db, lexer_t *l) { (void)db; lex_next(l); prog_public(l); }
static void h_cancel(dbf_t *db, lexer_t *l) { (void)db; (void)l; if (prog_is_running()) prog_cancel(); }
static void h_retry(dbf_t *db, lexer_t *l) { (void)db; (void)l; prog_retry(); }
static void h_suspend(dbf_t *db, lexer_t *l) { (void)db; (void)l; if (prog_is_running()) prog_suspend(); else printf("Not in program.\n"); }
static void h_resume(dbf_t *db, lexer_t *l) { (void)db; (void)l; prog_resume(); }
static void h_eject(dbf_t *db, lexer_t *l) { (void)db; (void)l; screen_eject(); }
static void h_run(dbf_t *db, lexer_t *l) { (void)db; char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); prog_do(arg); }
static void h_select(dbf_t *db, lexer_t *l) {
    (void)db;
    char arg[256];
    lex_next(l); /* skip SELECT */
    lex_get_remaining(l, arg, sizeof(arg));
    cmd_select(arg);
}
static void h_use(dbf_t *db, lexer_t *l) { char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); cmd_use(db, arg); }
static void h_replace(dbf_t *db, lexer_t *l) { lex_next(l); cmd_replace(db, l); }
static void h_scatter(dbf_t *db, lexer_t *l) { lex_next(l); cmd_scatter(db, l); }
static void h_gather(dbf_t *db, lexer_t *l) { lex_next(l); cmd_gather(db, l); }
static void h_list(dbf_t *db, lexer_t *l) {
    if (cmd_peek_kw(l, "STRUCTURE")) {
        lex_next(l); /* skip LIST */
        lex_next(l); /* skip STRUCTURE */
        cmd_display_structure(db);
    } else if (cmd_peek_kw(l, "FILES")) {
        char pattern[64];
        lex_next(l); /* skip LIST */
        lex_next(l); /* skip FILES */
        str_copy(pattern, "*.*", sizeof(pattern));
        if (cmd_kw(l, "LIKE")) {
            lex_next(l); /* skip LIKE */
            if (l->current.type == TOK_IDENT || l->current.type == TOK_STRING) {
                str_copy(pattern, l->current.text, sizeof(pattern));
            }
        }
        /* List matching files */
        {
            DIR *dir = opendir(".");
            struct dirent *ent;
            int count = 0;
            if (dir) {
                while ((ent = readdir(dir)) != NULL) {
                    if (ent->d_name[0] == '.') continue;
                    if (str_like(ent->d_name, pattern)) {
                        printf("%-16s\n", ent->d_name);
                        count++;
                    }
                }
                closedir(dir);
            }
            printf("%d file(s) found.\n", count);
        }
    } else {
        lex_next(l);
        cmd_list(db, l);
    }
}
static void h_display(dbf_t *db, lexer_t *l) {
    if (cmd_peek_kw(l, "STRUCTURE")) {
        lex_next(l); /* skip DISPLAY */
        lex_next(l); /* skip STRUCTURE */
        cmd_display_structure(db);
    } else if (cmd_peek_kw(l, "MEMORY")) {
        lex_next(l); /* skip DISPLAY */
        lex_next(l); /* skip MEMORY */
        memvar_display(&memvar_store);
    } else if (cmd_peek_kw(l, "STATUS")) {
        lex_next(l); /* skip DISPLAY */
        lex_next(l); /* skip STATUS */
        cmd_display_status();
    } else {
        lex_next(l);
        cmd_display(db, l);
    }
}
static void h_store(dbf_t *db, lexer_t *l) { lex_next(l); cmd_store(db, l); }
static void h_release(dbf_t *db, lexer_t *l) {
    (void)db;
    lex_next(l); /* skip RELEASE */
    if (cmd_kw(l, "POPUPS")) {
        menu_release_all();
        return;
    }
    if (cmd_kw(l, "POPUP")) {
        char name[32];
        lex_next(l); /* skip POPUP */
        if (l->current.type != TOK_IDENT) {
            printf("Syntax: RELEASE POPUP <name>\n");
            return;
        }
        str_copy(name, l->current.text, sizeof(name));
        menu_release_popup(name);
        return;
    }
    if (cmd_kw(l, "MENUS")) {
        menu_release_all_menus();
        return;
    }
    if (cmd_kw(l, "MENU")) {
        char name[32];
        lex_next(l); /* skip MENU */
        if (l->current.type != TOK_IDENT) {
            printf("Syntax: RELEASE MENU <name>\n");
            return;
        }
        str_copy(name, l->current.text, sizeof(name));
        menu_release_menu(name);
        return;
    }
    {
        char arg[256];
        lex_get_remaining(l, arg, sizeof(arg));
        cmd_release(arg);
    }
}
static void h_skip(dbf_t *db, lexer_t *l) { char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); cmd_skip(db, arg); follow_relations(); }
static void h_locate(dbf_t *db, lexer_t *l) { lex_next(l); cmd_locate(db, l); follow_relations(); }
static void h_continue(dbf_t *db, lexer_t *l) { (void)l; cmd_continue(db); follow_relations(); }
static void h_count(dbf_t *db, lexer_t *l) { lex_next(l); cmd_count(db, l); }
static void h_sum(dbf_t *db, lexer_t *l) { lex_next(l); cmd_sum(db, l); }
static void h_average(dbf_t *db, lexer_t *l) { lex_next(l); cmd_average(db, l); }
static void h_calculate(dbf_t *db, lexer_t *l) { lex_next(l); cmd_calculate(db, l); }
static void h_reindex(dbf_t *db, lexer_t *l) { (void)l; cmd_reindex(db); }
static void h_delete(dbf_t *db, lexer_t *l) {
    lex_next(l); /* skip DELETE */
    if (cmd_kw(l, "FILE")) {
        char arg[256];
        lex_next(l); /* skip FILE */
        lex_get_remaining(l, arg, sizeof(arg));
        cmd_erase(arg);
        return;
    }
    cmd_delete(db, l);
}
static void h_recall(dbf_t *db, lexer_t *l) { lex_next(l); cmd_recall(db, l); }
static void h_pack(dbf_t *db, lexer_t *l) { (void)l; cmd_pack(db); }
static void h_zap(dbf_t *db, lexer_t *l) { (void)l; cmd_zap(db); }
static void h_erase(dbf_t *db, lexer_t *l) { (void)db; char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); cmd_erase(arg); }
static void h_rename(dbf_t *db, lexer_t *l) { (void)db; char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); cmd_rename(arg); }
static void h_accept(dbf_t *db, lexer_t *l) { (void)db; char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); cmd_accept(arg); }
static void h_input(dbf_t *db, lexer_t *l) { (void)db; char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); cmd_input(arg); }
static void h_wait(dbf_t *db, lexer_t *l) { (void)db; char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); cmd_wait(arg); }
static void h_read(dbf_t *db, lexer_t *l) { (void)db; (void)l; screen_read(); }

static void h_menu(dbf_t *db, lexer_t *l) {
    char varname[MEMVAR_NAMELEN];
    (void)db;
    lex_next(l);  /* skip MENU */
    if (!cmd_kw(l, "TO")) {
        printf("Syntax: MENU TO <memvar>\n");
        return;
    }
    lex_next(l);  /* skip TO */
    if (l->current.type != TOK_IDENT) {
        printf("Syntax: MENU TO <memvar>\n");
        return;
    }
    str_copy(varname, l->current.text, sizeof(varname));
    {
        int result = screen_menu_to();
        value_t v = val_num((double)result);
        memvar_set(cmd_get_memvar_store(), varname, &v);
    }
}

static void h_go(dbf_t *db, lexer_t *l) {
    char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg));
    cmd_go(db, arg); follow_relations();
}

static void h_on(dbf_t *db, lexer_t *l) {
    (void)db;
    lex_next(l); /* skip ON */
    if (cmd_kw(l, "ERROR")) {
        lex_next(l);
        if (cmd_kw(l, "DO")) {
            lex_next(l);
            if (l->current.type == TOK_IDENT) {
                prog_on_error(l->current.text);
                lex_next(l);
            } else prog_on_error(NULL);
        } else prog_on_error(NULL);
    } else if (cmd_kw(l, "KEY")) {
        /* ON KEY — clear all key handlers */
        screen_clear_key_handlers();
    } else if (cmd_kw(l, "PAD")) {
        /* ON PAD <padname> OF <menu> ACTIVATE POPUP <popup> */
        char pad_name[32], menu_name[32], popup_name[32];
        lex_next(l); /* skip PAD */
        if (l->current.type != TOK_IDENT) {
            printf("Syntax: ON PAD <pad> OF <menu> ACTIVATE POPUP <popup>\n");
            return;
        }
        str_copy(pad_name, l->current.text, sizeof(pad_name));
        lex_next(l); /* skip pad name */
        if (!cmd_kw(l, "OF")) {
            printf("Expected OF after pad name.\n");
            return;
        }
        lex_next(l); /* skip OF */
        if (l->current.type != TOK_IDENT) {
            printf("Expected menu name after OF.\n");
            return;
        }
        str_copy(menu_name, l->current.text, sizeof(menu_name));
        lex_next(l); /* skip menu name */
        if (!cmd_kw(l, "ACTIVATE")) {
            printf("Expected ACTIVATE POPUP after menu name.\n");
            return;
        }
        lex_next(l); /* skip ACTIVATE */
        if (!cmd_kw(l, "POPUP")) {
            printf("Expected POPUP after ACTIVATE.\n");
            return;
        }
        lex_next(l); /* skip POPUP */
        if (l->current.type != TOK_IDENT) {
            printf("Expected popup name.\n");
            return;
        }
        str_copy(popup_name, l->current.text, sizeof(popup_name));
        menu_set_pad_popup(menu_name, pad_name, popup_name);
    } else if (cmd_kw(l, "SELECTION")) {
        lex_next(l); /* skip SELECTION */
        if (cmd_kw(l, "POPUP")) {
            char popup_name[32], procname[64];
            lex_next(l); /* skip POPUP */
            if (l->current.type != TOK_IDENT) {
                printf("Expected popup name.\n");
                return;
            }
            str_copy(popup_name, l->current.text, sizeof(popup_name));
            lex_next(l); /* skip popup name */
            if (!cmd_kw(l, "DO")) {
                printf("Expected DO.\n");
                return;
            }
            lex_next(l); /* skip DO */
            if (l->current.type != TOK_IDENT) {
                printf("Expected procedure name.\n");
                return;
            }
            str_copy(procname, l->current.text, sizeof(procname));
            menu_set_on_selection_popup(popup_name, procname);
        } else if (cmd_kw(l, "PAD")) {
            char pad_name[32], menu_name[32], procname[64];
            lex_next(l); /* skip PAD */
            if (l->current.type != TOK_IDENT) {
                printf("Expected pad name.\n");
                return;
            }
            str_copy(pad_name, l->current.text, sizeof(pad_name));
            lex_next(l); /* skip pad name */
            if (!cmd_kw(l, "OF")) {
                printf("Expected OF after pad name.\n");
                return;
            }
            lex_next(l); /* skip OF */
            if (l->current.type != TOK_IDENT) {
                printf("Expected menu name after OF.\n");
                return;
            }
            str_copy(menu_name, l->current.text, sizeof(menu_name));
            lex_next(l); /* skip menu name */
            if (!cmd_kw(l, "DO")) {
                printf("Expected DO.\n");
                return;
            }
            lex_next(l); /* skip DO */
            if (l->current.type != TOK_IDENT) {
                printf("Expected procedure name.\n");
                return;
            }
            str_copy(procname, l->current.text, sizeof(procname));
            menu_set_on_selection_pad(menu_name, pad_name, procname);
        } else {
            printf("Syntax: ON SELECTION POPUP <name> DO <proc>\n");
            printf("    or: ON SELECTION PAD <pad> OF <menu> DO <proc>\n");
        }
    }
}

static void h_report(dbf_t *db, lexer_t *l) {
    lex_next(l); /* skip REPORT */
    if (cmd_kw(l, "FORM")) {
        lex_next(l);
        cmd_report_form(db, l);
    }
}

static void h_label(dbf_t *db, lexer_t *l) {
    lex_next(l); /* skip LABEL */
    if (cmd_kw(l, "FORM")) {
        lex_next(l);
        char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
        cmd_label_form(db, arg);
    }
}

static void h_create(dbf_t *db, lexer_t *l) {
    lex_next(l); /* skip CREATE */
    if (cmd_kw(l, "REPORT")) {
        lex_next(l);
        char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
        cmd_create_report(arg);
    } else if (cmd_kw(l, "LABEL")) {
        lex_next(l);
        char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
        cmd_create_label(arg);
    } else {
        char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
        cmd_create(db, arg);
    }
}

static void h_append(dbf_t *db, lexer_t *l) {
    lex_next(l); /* skip APPEND */
    if (cmd_kw(l, "BLANK")) {
        cmd_append_blank(db);
        lex_next(l);
    } else if (cmd_kw(l, "FROM")) {
        lex_next(l);
        char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
        cmd_append_from(db, arg);
    } else printf("Syntax: APPEND BLANK | APPEND FROM <filename>\n");
}

static void h_copy(dbf_t *db, lexer_t *l) {
    lex_next(l); /* skip COPY */
    if (cmd_kw(l, "STRUCTURE")) {
        lex_next(l);
        if (cmd_kw(l, "EXTENDED")) {
            lex_next(l);
            if (cmd_kw(l, "TO")) {
                lex_next(l);
                char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
                cmd_copy_structure_extended(db, arg);
            } else printf("Syntax: COPY STRUCTURE EXTENDED TO <filename>\n");
        } else if (cmd_kw(l, "TO")) {
            lex_next(l);
            char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
            cmd_copy_structure(db, arg);
        } else printf("Syntax: COPY STRUCTURE TO <filename>\n");
    } else if (cmd_kw(l, "FILE")) {
        lex_next(l);
        char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
        cmd_copy_file(arg);
    } else if (cmd_kw(l, "TO")) {
        lex_next(l);
        char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
        cmd_copy_to(db, arg);
    }
}

static void h_sort(dbf_t *db, lexer_t *l) {
    lex_next(l); /* skip SORT */
    if (cmd_kw(l, "TO")) {
        lex_next(l);
        char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
        cmd_sort(db, arg);
    } else printf("Syntax: SORT TO <filename> ON <field> [/A][/D][/C]\n");
}

static void h_index(dbf_t *db, lexer_t *l) { lex_next(l); cmd_index_on(db, l); }

static void h_seek(dbf_t *db, lexer_t *l) { char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); cmd_seek(db, arg); }
static void h_find(dbf_t *db, lexer_t *l) { char arg[256]; lex_next(l); lex_get_remaining(l, arg, sizeof(arg)); cmd_find(db, arg); }

static void h_close(dbf_t *db, lexer_t *l) {
    lex_next(l); /* skip CLOSE */
    if (cmd_kw(l, "DATABASES") || cmd_kw(l, "DATA")) {
        cmd_close_all();
        lex_next(l);
    } else if (cmd_kw(l, "ALL")) {
        cmd_close_all();
        memvar_release_all(&memvar_store);
        prog_set_procedure(NULL);
        lex_next(l);
    } else if (cmd_kw(l, "INDEX")) {
        close_all_indexes(cur_wa());
        lex_next(l);
    } else if (cmd_kw(l, "PROCEDURE")) {
        prog_set_procedure(NULL);
        lex_next(l);
    } else if (cmd_kw(l, "ALTERNATE")) {
        if (set_opts.alternate_fp) {
            fclose(set_opts.alternate_fp);
            set_opts.alternate_fp = NULL;
        }
        set_opts.alternate_on = 0;
        lex_next(l);
    } else if (dbf_is_open(db)) {
        dbf_close(db);
        close_all_indexes(cur_wa());
        cur_wa()->alias[0] = '\0';
        cur_wa()->filter_cond[0] = '\0';
        if (cur_wa()->filter_ast) { ast_free(cur_wa()->filter_ast); cur_wa()->filter_ast = NULL; }
        cur_wa()->order = 0;
        cur_wa()->num_indexes = 0;
    }
}

static void h_clear(dbf_t *db, lexer_t *l) {
    lex_next(l); /* skip CLEAR */
    if (cmd_kw(l, "ALL")) {
        cmd_close_all();
        memvar_release_all(&memvar_store);
        prog_set_procedure(NULL);
        lex_next(l);
    } else if (cmd_kw(l, "MEMORY")) {
        memvar_release_all(&memvar_store);
        lex_next(l);
    } else if (cmd_kw(l, "GETS")) {
        screen_clear_gets();
        lex_next(l);
    } else {
        screen_clear();
    }
}

static void h_set(dbf_t *db, lexer_t *l) {
    lex_next(l); /* skip SET */
    if (cmd_kw(l, "COLOR")) {
        lex_next(l);
        if (cmd_kw(l, "TO")) {
            lex_next(l);
            char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
            screen_set_color(arg);
        }
    } else if (cmd_kw(l, "INDEX")) {
        lex_next(l);
        if (cmd_kw(l, "TO")) {
            lex_next(l);
            char arg[256];
            lex_get_remaining(l, arg, sizeof(arg));
            cmd_set_index(db, arg);
        }
    } else if (cmd_kw(l, "PROCEDURE")) {
        lex_next(l);
        if (cmd_kw(l, "TO")) {
            lex_next(l);
            char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
            prog_set_procedure(arg);
        }
    } else if (cmd_kw(l, "FILTER")) {
        lex_next(l);
        if (cmd_kw(l, "TO")) {
            lex_next(l);
            char arg[256]; lex_get_remaining(l, arg, sizeof(arg));
            if (arg[0] == '\0') {
                cur_wa()->filter_cond[0] = '\0';
                if (cur_wa()->filter_ast) { ast_free(cur_wa()->filter_ast); cur_wa()->filter_ast = NULL; }
                if (set_opts.talk) printf("Filter removed.\n");
            } else {
                const char *ast_err;
                str_copy(cur_wa()->filter_cond, arg, sizeof(cur_wa()->filter_cond));
                if (cur_wa()->filter_ast) { ast_free(cur_wa()->filter_ast); cur_wa()->filter_ast = NULL; }
                if (!strchr(cur_wa()->filter_cond, '&'))
                    cur_wa()->filter_ast = ast_compile(cur_wa()->filter_cond, &memvar_store, &ast_err);
                trim_right(arg);
                if (set_opts.talk) printf("Filter: %s\n", arg);
            }
        }
    } else if (cmd_kw(l, "ORDER")) {
        lex_next(l);
        if (cmd_kw(l, "TO")) {
            lex_next(l);
            if (l->current.type == TOK_NUMBER) {
                int n = (int)l->current.num_val;
                if (n >= 0 && n <= cur_wa()->num_indexes) {
                    cur_wa()->order = n;
                    if (n == 0) printf("Natural record order.\n");
                    else printf("Order set to %d (%s).\n", n, cur_wa()->indexes[n - 1].filename);
                }
                lex_next(l);
            }
        }
    } else if (cmd_kw(l, "KEY")) {
        /* SET KEY <keycode> TO [<procedure>] */
        lex_next(l);
        if (l->current.type == TOK_NUMBER || l->current.type == TOK_MINUS) {
            int neg = 0;
            int keycode;
            if (l->current.type == TOK_MINUS) {
                neg = 1;
                lex_next(l);
            }
            keycode = (l->current.type == TOK_NUMBER) ? (int)l->current.num_val : 0;
            if (neg) keycode = -keycode;
            lex_next(l);
            if (cmd_kw(l, "TO")) {
                lex_next(l);
                if (l->current.type == TOK_IDENT) {
                    screen_set_key_handler(keycode, l->current.text);
                    lex_next(l);
                } else {
                    screen_set_key_handler(keycode, NULL);
                }
            }
        }
    } else if (cmd_kw(l, "RELATION")) {
        lex_next(l);
        if (cmd_kw(l, "TO")) {
            lex_next(l);
            if (l->current.type == TOK_EOF) {
                cur_wa()->relation_expr[0] = '\0';
                cur_wa()->relation_target = -1;
            } else {
                const char *expr_start = l->token_start;
                while (l->current.type != TOK_EOF && !cmd_kw(l, "INTO"))
                    lex_next(l);
                
                if (l->current.type == TOK_EOF) {
                    printf("Syntax: SET RELATION TO <expr> INTO <alias>\n");
                    return;
                }
                
                int len = (int)(l->token_start - expr_start);
                if (len >= (int)sizeof(cur_wa()->relation_expr))
                    len = sizeof(cur_wa()->relation_expr) - 1;
                memcpy(cur_wa()->relation_expr, expr_start, len);
                cur_wa()->relation_expr[len] = '\0';
                trim_right(cur_wa()->relation_expr);
                
                lex_next(l); /* skip INTO */
                if (l->current.type != TOK_IDENT && l->current.type != TOK_NUMBER) {
                    prog_error(ERR_ALIAS_NOT_FOUND, "Invalid work area");
                    return;
                }
                cur_wa()->relation_target = area_resolve_alias(l->current.text);
                if (cur_wa()->relation_target < 0) {
                    prog_error(ERR_ALIAS_NOT_FOUND, "Invalid work area");
                }
                lex_next(l);
            }
        }
    } else if (cmd_kw(l, "ALTERNATE")) {
        lex_next(l);
        if (cmd_kw(l, "TO")) {
            lex_next(l);
            if (l->current.type == TOK_EOF) {
                /* SET ALTERNATE TO (no file) — close alternate */
                if (set_opts.alternate_fp) {
                    fclose(set_opts.alternate_fp);
                    set_opts.alternate_fp = NULL;
                }
                set_opts.alternate_on = 0;
            } else {
                char fname[128];
                fname[0] = '\0';
                while (l->current.type != TOK_EOF) {
                    strncat(fname, l->current.text, sizeof(fname) - strlen(fname) - 1);
                    lex_next(l);
                }
                trim_right(fname);
                if (set_opts.alternate_fp) {
                    fclose(set_opts.alternate_fp);
                    set_opts.alternate_fp = NULL;
                }
                set_opts.alternate_fp = fopen(fname, "w");
                if (!set_opts.alternate_fp) {
                    prog_error_fmt(ERR_CANNOT_CREATE, "Cannot create %s", fname);
                }
            }
        } else {
            /* SET ALTERNATE ON/OFF */
            char arg[256];
            snprintf(arg, sizeof(arg), "ALTERNATE %s", l->current.text);
            while (l->current.type != TOK_EOF) lex_next(l);
            set_execute(&set_opts, arg);
        }
    } else {
        char arg[256];
        lex_get_remaining(l, arg, sizeof(arg));
        set_execute(&set_opts, arg);
    }
}

static void h_modify(dbf_t *db, lexer_t *l) {
    lexer_t t = *l; lex_next(&t);
    if (cmd_kw(&t, "REPORT") || cmd_kw(&t, "LABEL")) printf("MODIFY not implemented.\n");
}

static void h_save(dbf_t *db, lexer_t *l) {
    (void)db;
    lex_next(l); /* skip SAVE */
    if (cmd_kw(l, "SCREEN")) {
        screen_save();
        return;
    }
    if (cmd_kw(l, "TO")) {
        lex_next(l);
        char fname[128];
        FILE *fp;
        fname[0] = '\0';
        while (l->current.type != TOK_EOF && !cmd_kw(l, "ALL")) {
            strncat(fname, l->current.text, sizeof(fname) - strlen(fname) - 1);
            lex_next(l);
        }
        str_upper(fname);
        if (strlen(fname) < 4 || str_icmp(fname + strlen(fname) - 4, ".MEM") != 0) strcat(fname, ".MEM");
        fp = fopen(fname, "wb");
        if (!fp) { prog_error_fmt(ERR_CANNOT_CREATE, "Cannot create %s", fname); return; }
        {
            int nv = 0, j;
            for (j = 0; j < MEMVAR_MAX; j++) { if (memvar_store.vars[j].used) nv++; }
            fwrite(&nv, 4, 1, fp);
            for (j = 0; j < MEMVAR_MAX; j++) {
                if (!memvar_store.vars[j].used) continue;
                fwrite(memvar_store.vars[j].name, MEMVAR_NAMELEN, 1, fp);
                fwrite(&memvar_store.vars[j].val.type, 4, 1, fp);
                if (memvar_store.vars[j].val.type == VAL_NUM) fwrite(&memvar_store.vars[j].val.num, 8, 1, fp);
                else if (memvar_store.vars[j].val.type == VAL_CHAR) {
                    int len = strlen(memvar_store.vars[j].val.str);
                    fwrite(&len, 4, 1, fp); fwrite(memvar_store.vars[j].val.str, len, 1, fp);
                } else if (memvar_store.vars[j].val.type == VAL_DATE) fwrite(&memvar_store.vars[j].val.date, 4, 1, fp);
                else if (memvar_store.vars[j].val.type == VAL_LOGIC) fwrite(&memvar_store.vars[j].val.logic, 4, 1, fp);
            }
        }
        fclose(fp);
    }
}

static void h_restore(dbf_t *db, lexer_t *l) {
    (void)db;
    lex_next(l); /* skip RESTORE */
    if (cmd_kw(l, "SCREEN")) {
        screen_restore();
        return;
    }
    if (cmd_kw(l, "FROM")) {
        lex_next(l);
        char fname[128];
        int additive = 0;
        FILE *fp;
        fname[0] = '\0';
        while (l->current.type != TOK_EOF && !cmd_kw(l, "ADDITIVE")) {
            strncat(fname, l->current.text, sizeof(fname) - strlen(fname) - 1);
            lex_next(l);
        }
        if (cmd_kw(l, "ADDITIVE")) {
            additive = 1;
            lex_next(l);
        }
        str_upper(fname);
        if (strlen(fname) < 4 || str_icmp(fname + strlen(fname) - 4, ".MEM") != 0) strcat(fname, ".MEM");
        fp = fopen(fname, "rb");
        if (!fp) { file_not_found(fname); return; }
        if (!additive) memvar_release_all(&memvar_store);
        {
            int nv = 0, j; fread(&nv, 4, 1, fp);
            for (j = 0; j < nv; j++) {
                char name[MEMVAR_NAMELEN]; int type; value_t v;
                fread(name, MEMVAR_NAMELEN, 1, fp); fread(&type, 4, 1, fp);
                v.type = type;
                if (type == VAL_NUM) fread(&v.num, 8, 1, fp);
                else if (type == VAL_CHAR) {
                    int len = 0; fread(&len, 4, 1, fp);
                    if (len >= (int)sizeof(v.str)) len = sizeof(v.str) - 1;
                    fread(v.str, len, 1, fp); v.str[len] = '\0';
                } else if (type == VAL_DATE) fread(&v.date, 4, 1, fp);
                else if (type == VAL_LOGIC) fread(&v.logic, 4, 1, fp);
                memvar_set(&memvar_store, name, &v);
            }
        }
        fclose(fp);
    }
}

/* ---- TYPE <filename> ---- */
static void cmd_type(const char *arg) {
    char filename[64];
    char line[256];
    FILE *f;
    const char *p = skip_ws(arg);
    str_copy(filename, p, sizeof(filename));
    trim_right(filename);
    path_normalize(filename);
    f = fopen(filename, "r");
    if (!f) {
        str_upper(filename);
        f = fopen(filename, "r");
    }
    if (!f) {
        printf("File not found.\n");
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        printf("%s", line);
    }
    fclose(f);
}

static void h_type(dbf_t *db, lexer_t *l) {
    char arg[256];
    (void)db;
    lex_next(l);
    lex_get_remaining(l, arg, sizeof(arg));
    cmd_type(arg);
}

/* ---- BLANK ---- */
static void cmd_blank(dbf_t *db) {
    int i;
    if (!dbf_is_open(db)) {
        prog_error(ERR_NO_DATABASE, "No database in use");
        return;
    }
    if (db->current_record == 0) {
        printf("No current record.\n");
        return;
    }
    for (i = 0; i < db->field_count; i++) {
        char buf[256];
        int len = db->fields[i].length;
        switch (db->fields[i].type) {
        case 'L':
            dbf_set_field_raw(db, i, "F");
            break;
        default:
            memset(buf, ' ', len);
            buf[len] = '\0';
            dbf_set_field_raw(db, i, buf);
            break;
        }
    }
    dbf_flush_record(db);
}

static void h_blank(dbf_t *db, lexer_t *l) {
    (void)l;
    cmd_blank(db);
}

static void h_total(dbf_t *db, lexer_t *l) { (void)db; (void)l; printf("TOTAL ON not implemented.\n"); }
static void h_join(dbf_t *db, lexer_t *l) { (void)db; (void)l; printf("JOIN WITH not implemented.\n"); }
static void h_update(dbf_t *db, lexer_t *l) { (void)db; (void)l; printf("UPDATE ON not implemented.\n"); }
/* QUIT and EXIT are handled directly in cmd_execute() because they
   must return 1 to signal the interpreter to exit.  They are NOT in
   the dispatch table. */
static void h_stub(dbf_t *db, lexer_t *l) { (void)db; (void)l; printf("Command not implemented.\n"); }

static void h_browse(dbf_t *db, lexer_t *l) {
    char arg[256];
    lex_next(l);
    lex_get_remaining(l, arg, sizeof(arg));
    cmd_browse(db, arg);
}
static void h_edit(dbf_t *db, lexer_t *l) {
    char arg[256];
    lex_next(l);
    lex_get_remaining(l, arg, sizeof(arg));
    cmd_edit(db, arg);
}

/* ---- Public wrappers for browse.c ---- */
index_t *cmd_controlling_index(void)    { return controlling_index(); }
int cmd_skip_deleted(const char *buf)   { return skip_deleted(buf); }
int cmd_check_filter(dbf_t *db)         { return check_filter(db); }
void cmd_follow_relations(void)         { follow_relations(); }
int cmd_field_display_width(dbf_t *db, int f) { return field_display_width(db, f); }
void cmd_indexes_capture_keys(dbf_t *db, char keys[][MAX_INDEX_KEY+1])  { indexes_capture_keys(db, keys); }
int cmd_indexes_update_current(dbf_t *db, char keys[][MAX_INDEX_KEY+1]) { return indexes_update_current(db, keys); }
void cmd_indexes_insert_current(dbf_t *db) { indexes_insert_current(db); }
void cmd_ctx_setup(void)                { ctx_setup(); }
int cmd_get_deleted(void)               { return set_opts.deleted; }

/* ---- DEFINE POPUP / DEFINE BAR ---- */
static void h_define(dbf_t *db, lexer_t *l) {
    (void)db;
    lex_next(l); /* skip DEFINE */
    if (cmd_kw(l, "POPUP")) {
        char name[32];
        int r1, c1, r2 = 0, c2 = 0;
        lex_next(l); /* skip POPUP */
        if (l->current.type != TOK_IDENT) {
            printf("Syntax: DEFINE POPUP <name> FROM r1,c1 [TO r2,c2]\n");
            return;
        }
        str_copy(name, l->current.text, sizeof(name));
        lex_next(l); /* skip name */
        if (!cmd_kw(l, "FROM")) {
            printf("Syntax: DEFINE POPUP <name> FROM r1,c1 [TO r2,c2]\n");
            return;
        }
        lex_next(l); /* skip FROM */
        /* Parse r1 */
        if (l->current.type != TOK_NUMBER) {
            printf("Expected row number after FROM.\n");
            return;
        }
        r1 = (int)l->current.num_val;
        lex_next(l); /* skip r1 */
        /* Expect comma */
        if (l->current.type == TOK_COMMA) lex_next(l);
        /* Parse c1 */
        if (l->current.type != TOK_NUMBER) {
            printf("Expected column number.\n");
            return;
        }
        c1 = (int)l->current.num_val;
        lex_next(l); /* skip c1 */
        /* Optional TO r2,c2 */
        if (cmd_kw(l, "TO")) {
            lex_next(l); /* skip TO */
            if (l->current.type != TOK_NUMBER) {
                printf("Expected row number after TO.\n");
                return;
            }
            r2 = (int)l->current.num_val;
            lex_next(l); /* skip r2 */
            if (l->current.type == TOK_COMMA) lex_next(l);
            if (l->current.type != TOK_NUMBER) {
                printf("Expected column number.\n");
                return;
            }
            c2 = (int)l->current.num_val;
        }
        menu_define_popup(name, r1, c1, r2, c2);
    } else if (cmd_kw(l, "BAR")) {
        int bar_num;
        char popup_name[32];
        char prompt[80] = "";
        char message[160] = "";
        char skip_expr[256] = "";
        lex_next(l); /* skip BAR */
        /* Parse bar number */
        if (l->current.type != TOK_NUMBER) {
            printf("Syntax: DEFINE BAR <n> OF <popup> PROMPT \"text\"\n");
            return;
        }
        bar_num = (int)l->current.num_val;
        lex_next(l); /* skip number */
        if (!cmd_kw(l, "OF")) {
            printf("Expected OF after bar number.\n");
            return;
        }
        lex_next(l); /* skip OF */
        if (l->current.type != TOK_IDENT) {
            printf("Expected popup name after OF.\n");
            return;
        }
        str_copy(popup_name, l->current.text, sizeof(popup_name));
        lex_next(l); /* skip popup name */
        if (!cmd_kw(l, "PROMPT")) {
            printf("Expected PROMPT keyword.\n");
            return;
        }
        lex_next(l); /* skip PROMPT */
        if (l->current.type == TOK_STRING) {
            str_copy(prompt, l->current.text, sizeof(prompt));
            lex_next(l);
        } else {
            /* Evaluate expression for prompt text */
            value_t pv;
            const char *ep = l->token_start;
            if (ep && expr_eval(cmd_get_expr_ctx(), &ep, &pv) == 0) {
                val_to_string(&pv, prompt, sizeof(prompt));
                trim_right(prompt);
                lexer_init_ext(l, ep, l->store);
            }
        }
        /* Optional MESSAGE */
        if (cmd_kw(l, "MESSAGE")) {
            lex_next(l); /* skip MESSAGE */
            if (l->current.type == TOK_STRING) {
                str_copy(message, l->current.text, sizeof(message));
                lex_next(l);
            }
        }
        /* Optional SKIP [FOR <expr>] */
        if (cmd_kw(l, "SKIP")) {
            lex_next(l); /* skip SKIP */
            if (cmd_kw(l, "FOR")) {
                lex_next(l); /* skip FOR */
                /* Remainder is the skip expression */
                lex_get_remaining(l, skip_expr, sizeof(skip_expr));
            } else {
                /* bare SKIP = always skip */
                str_copy(skip_expr, ".T.", sizeof(skip_expr));
            }
        }
        menu_define_bar(popup_name, bar_num, prompt, message, skip_expr);
    } else if (cmd_kw(l, "MENU")) {
        char name[32];
        lex_next(l); /* skip MENU */
        if (l->current.type != TOK_IDENT) {
            printf("Syntax: DEFINE MENU <name>\n");
            return;
        }
        str_copy(name, l->current.text, sizeof(name));
        menu_define_menu(name);
    } else if (cmd_kw(l, "PAD")) {
        char pad_name[32], menu_name[32];
        char prompt[80] = "";
        char message[160] = "";
        int row = -1, col = -1;
        lex_next(l); /* skip PAD */
        if (l->current.type != TOK_IDENT) {
            printf("Syntax: DEFINE PAD <name> OF <menu> PROMPT \"text\"\n");
            return;
        }
        str_copy(pad_name, l->current.text, sizeof(pad_name));
        lex_next(l); /* skip pad name */
        if (!cmd_kw(l, "OF")) {
            printf("Expected OF after pad name.\n");
            return;
        }
        lex_next(l); /* skip OF */
        if (l->current.type != TOK_IDENT) {
            printf("Expected menu name after OF.\n");
            return;
        }
        str_copy(menu_name, l->current.text, sizeof(menu_name));
        lex_next(l); /* skip menu name */
        if (!cmd_kw(l, "PROMPT")) {
            printf("Expected PROMPT keyword.\n");
            return;
        }
        lex_next(l); /* skip PROMPT */
        if (l->current.type == TOK_STRING) {
            str_copy(prompt, l->current.text, sizeof(prompt));
            lex_next(l);
        } else {
            value_t pv;
            const char *ep = l->token_start;
            if (ep && expr_eval(cmd_get_expr_ctx(), &ep, &pv) == 0) {
                val_to_string(&pv, prompt, sizeof(prompt));
                trim_right(prompt);
                lexer_init_ext(l, ep, l->store);
            }
        }
        /* Optional AT row,col */
        if (cmd_kw(l, "AT")) {
            lex_next(l); /* skip AT */
            if (l->current.type == TOK_NUMBER) {
                row = (int)l->current.num_val;
                lex_next(l);
                if (l->current.type == TOK_COMMA) lex_next(l);
                if (l->current.type == TOK_NUMBER) {
                    col = (int)l->current.num_val;
                    lex_next(l);
                }
            }
        }
        /* Optional MESSAGE */
        if (cmd_kw(l, "MESSAGE")) {
            lex_next(l); /* skip MESSAGE */
            if (l->current.type == TOK_STRING) {
                str_copy(message, l->current.text, sizeof(message));
                lex_next(l);
            }
        }
        menu_define_pad(menu_name, pad_name, prompt, row, col, message);
    } else {
        printf("Syntax: DEFINE POPUP|BAR|MENU|PAD ...\n");
    }
}

/* ---- ACTIVATE POPUP / ACTIVATE MENU ---- */
static void h_activate(dbf_t *db, lexer_t *l) {
    (void)db;
    lex_next(l); /* skip ACTIVATE */
    if (cmd_kw(l, "POPUP")) {
        char name[32];
        lex_next(l); /* skip POPUP */
        if (l->current.type != TOK_IDENT) {
            printf("Syntax: ACTIVATE POPUP <name>\n");
            return;
        }
        str_copy(name, l->current.text, sizeof(name));
        menu_activate_popup(name);
    } else if (cmd_kw(l, "MENU")) {
        char name[32];
        lex_next(l); /* skip MENU */
        if (l->current.type != TOK_IDENT) {
            printf("Syntax: ACTIVATE MENU <name>\n");
            return;
        }
        str_copy(name, l->current.text, sizeof(name));
        menu_activate_menu(name);
    } else {
        printf("Syntax: ACTIVATE POPUP|MENU <name>\n");
    }
}

/* ---- DEACTIVATE POPUP / DEACTIVATE MENU ---- */
static void h_deactivate(dbf_t *db, lexer_t *l) {
    (void)db;
    lex_next(l); /* skip DEACTIVATE */
    if (cmd_kw(l, "POPUP")) {
        menu_deactivate_popup();
    } else if (cmd_kw(l, "MENU")) {
        menu_deactivate_menu();
    } else {
        printf("Syntax: DEACTIVATE POPUP|MENU\n");
    }
}

static cmd_entry_t cmd_table[] = {
    { "ACCEPT", h_accept }, { "ACTIVATE", h_activate },
    { "APPEND", h_append },
    { "AVERAGE", h_average }, { "BLANK", h_blank }, { "BROWSE", h_browse },
    { "CALCULATE", h_calculate },
    { "CANCEL", h_cancel }, { "CASE", h_case },
    { "CHANGE", h_edit }, { "CLEAR", h_clear },
    { "CLOSE", h_close }, { "CONTINUE", h_continue },
    { "COPY", h_copy }, { "COUNT", h_count },
    { "CREATE", h_create },
    { "DEACTIVATE", h_deactivate }, { "DECLARE", h_declare },
    { "DEFINE", h_define },
    { "DELETE", h_delete }, { "DIMENSION", h_declare },
    { "DISPLAY", h_display }, { "DO", h_do },
    { "EDIT", h_edit }, { "EJECT", h_eject }, { "ELSE", h_else },
    { "ENDCASE", h_endcase }, { "ENDDO", h_enddo },
    { "ENDIF", h_endif }, { "ENDSCAN", h_endscan },
    { "ERASE", h_erase }, { "FIND", h_find },
    { "GATHER", h_gather }, { "GO", h_go }, { "GOTO", h_go },
    { "IF", h_if }, { "INDEX", h_index },
    { "INPUT", h_input }, { "JOIN", h_join },
    { "LABEL", h_label }, { "LIST", h_list },
    { "LOCATE", h_locate }, { "LOOP", h_loop },
    { "MENU", h_menu }, { "MODIFY", h_modify }, { "ON", h_on },
    { "OTHERWISE", h_otherwise }, { "PACK", h_pack },
    { "PARAMETERS", h_parameters }, { "PRIVATE", h_private },
    { "PROCEDURE", h_procedure }, { "PUBLIC", h_public },
    { "READ", h_read },
    { "RECALL", h_recall }, { "REINDEX", h_reindex },
    { "RELEASE", h_release }, { "RENAME", h_rename },
    { "REPLACE", h_replace }, { "REPORT", h_report },
    { "RESTORE", h_restore }, { "RESUME", h_resume },
    { "RETRY", h_retry }, { "RETURN", h_return },
    { "RUN", h_run }, { "SAVE", h_save },
    { "SCAN", h_scan }, { "SCATTER", h_scatter }, { "SEEK", h_seek },
    { "SELECT", h_select }, { "SET", h_set },
    { "SKIP", h_skip }, { "SORT", h_sort },
    { "STORE", h_store }, { "SUM", h_sum },
    { "SUSPEND", h_suspend }, { "TOTAL", h_total },
    { "TYPE", h_type }, { "UPDATE", h_update }, { "USE", h_use },
    { "WAIT", h_wait }, { "ZAP", h_zap },
    { NULL, NULL }
};

/* ---- Command dispatch helpers ---- */
static int cmd_table_count(void) {
    int i = 0;
    while (cmd_table[i].name) i++;
    return i;
}

static int cmd_table_exact(const char *ident) {
    int lo = 0;
    int hi = cmd_table_count();
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = str_icmp(cmd_table[mid].name, ident);
        if (cmp == 0) return mid;
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}

static int cmd_table_abbrev(const char *ident, int *ambiguous) {
    int ident_len = (int)strlen(ident);
    int lo = 0;
    int hi = cmd_table_count();
    int i;
    int match = -1;

    *ambiguous = 0;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = str_icmp(cmd_table[mid].name, ident);
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }

    for (i = lo; cmd_table[i].name; i++) {
        int name_len = (int)strlen(cmd_table[i].name);
        int min_len = (name_len < 4) ? name_len : 4;
        if (str_nicmp(cmd_table[i].name, ident, ident_len) != 0) break;
        if (ident_len < min_len || ident_len > name_len) continue;
        if (match >= 0) {
            *ambiguous = 1;
            return -1;
        }
        match = i;
    }
    return match;
}

int cmd_execute(dbf_t *db, char *line) {
    char *p;
    dbf_t *cdb;
    lexer_t l;
    int idx;
    int ambiguous;

    (void)db;
    ctx_setup();
    cdb = cur_db();

    p = skip_ws(line);
    if (*p == '\0') return 0;
    if (*p == '*') return 0;

    lexer_init_ext(&l, p, expr_ctx.vars);
    if (cmd_kw(&l, "NOTE")) return 0;

    /* Handle specific non-keyword prefix commands */
    if (p[0] == '@') { screen_at_cmd(p); return 0; }
    if (p[0] == '?') {
        if (p[1] == '?') cmd_print_expr(p + 2, 0);
        else cmd_print_expr(p + 1, 1);
        return 0;
    }

    /* Variable assignment: identifier[idx] = expr */
    if (is_ident_start(p[0])) {
        lexer_t al;
        lexer_init_ext(&al, p, expr_ctx.vars);
        if (al.current.type == TOK_IDENT) {
            char name[MEMVAR_NAMELEN];
            str_copy(name, al.current.text, sizeof(name));
            lex_next(&al);
            int is_indexed = 0;
            int row = 0, col = 1;

            int is_array = 0;
            value_t mv;
            if (memvar_find(&memvar_store, name, &mv) == 0 && mv.type == VAL_ARRAY)
                is_array = 1;

            if ((is_array && (al.current.type == TOK_LPAREN || al.current.type == TOK_LBRACKET)) ||
                (al.current.type == TOK_LBRACKET)) {
                token_type_t end_type = (al.current.type == TOK_LPAREN) ? TOK_RPAREN : TOK_RBRACKET;
                const char *next_p = al.token_start + 1; /* skip [ or ( */
                value_t row_v, col_v;
                if (expr_eval(&expr_ctx, &next_p, &row_v) == 0 && row_v.type == VAL_NUM) {
                    row = (int)row_v.num;
                    lexer_init_ext(&al, next_p, expr_ctx.vars);
                    if (al.current.type == TOK_COMMA) {
                        next_p = al.token_start + 1; /* skip , */
                        if (expr_eval(&expr_ctx, &next_p, &col_v) == 0 && col_v.type == VAL_NUM) {
                            col = (int)col_v.num;
                            lexer_init_ext(&al, next_p, expr_ctx.vars);
                        }
                    }
                    if (al.current.type == end_type) {
                        lex_next(&al);
                    }
                    is_indexed = 1;
                }
            }
            if (al.current.type == TOK_EQ) {
                if (lex_is_reserved(name)) {
                    char err[128];
                    snprintf(err, sizeof(err), "%s is a reserved keyword", name);
                    prog_error(ERR_SYNTAX, err);
                    return 0;
                }
                value_t val;
                const char *next_p = al.token_start + 1; /* skip = */
                if (expr_eval(&expr_ctx, &next_p, &val) != 0) {
                    report_expr_error();
                    return 0;
                }
                if (is_indexed) {
                    if (memvar_set_elem(&memvar_store, name, row, col, &val) == -2) {
                        prog_error(ERR_RECORD_RANGE, "Array index out of bounds");
                    }
                } else if (is_array) {
                    prog_error(ERR_SYNTAX, "Array assignment requires index");
                } else {
                    memvar_set(&memvar_store, name, &val);
                }
                return 0;
            }
        }
    }

    /* Standard command dispatch (exact match, then unique abbreviation) */
    if (l.current.type == TOK_IDENT) {
        idx = cmd_table_exact(l.current.text);
        if (idx >= 0) {
            cmd_table[idx].func(cdb, &l);
            return 0;
        }
        idx = cmd_table_abbrev(l.current.text, &ambiguous);
        if (idx >= 0) {
            cmd_table[idx].func(cdb, &l);
            return 0;
        }
        if (ambiguous) {
            prog_error(ERR_SYNTAX, "Ambiguous command");
            return 0;
        }
    }

    /* QUIT/EXIT return 1 to exit the interpreter.
       EXIT inside a program exits the current DO WHILE loop instead. */
    if (cmd_kw(&l, "QUIT")) return 1;
    if (cmd_kw(&l, "EXIT")) {
        if (prog_is_running()) { prog_exit_loop(); return 0; }
        return 1;
    }

    prog_error(ERR_UNRECOGNIZED, "Unrecognized command");
    return 0;
}
