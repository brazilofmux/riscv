#ifndef COMMAND_H
#define COMMAND_H

#include "dbf.h"
#include "memvar.h"
#include "expr.h"
#include "clause.h"
#include "set.h"
#include "index.h"

/* Execute a command line. Returns 1 if QUIT, 0 otherwise. */
int cmd_execute(dbf_t *db, char *line);

/* Close all open work areas */
void cmd_close_all(void);

/* Clause and scope utilities */
struct lexer;
void clause_init(clause_t *c);
int  parse_clauses(struct lexer *l, clause_t *c);
int  scope_bounds(dbf_t *db, const scope_t *s, uint32_t *start, uint32_t *end);
void clause_compile(clause_t *c, memvar_store_t *store);
void clause_free_ast(clause_t *c);

/* Access to shared state (for program.c, screen.c) */
memvar_store_t *cmd_get_memvar_store(void);
expr_ctx_t *cmd_get_expr_ctx(void);
int cmd_get_device(void);     /* 0=SCREEN, 1=PRINT */
int cmd_get_console(void);    /* SET CONSOLE ON/OFF */
int cmd_get_talk(void);       /* SET TALK ON/OFF */
int cmd_get_confirm(void);    /* SET CONFIRM ON/OFF */
int cmd_get_century(void);    /* SET CENTURY ON/OFF */
int cmd_get_escape(void);     /* SET ESCAPE ON/OFF */
int cmd_get_echo(void);       /* SET ECHO ON/OFF */
int cmd_get_margin(void);     /* SET MARGIN TO n */
int cmd_get_message_row(void); /* SET MESSAGE TO row (-1=disabled) */
int cmd_get_wrap(void);       /* SET WRAP ON/OFF */
date_format_t cmd_get_date_format(void);  /* SET DATE format */
int  cmd_get_memowidth(void);   /* SET MEMOWIDTH TO n */
int  cmd_get_epoch(void);       /* SET EPOCH TO year */
char cmd_get_mark(void);        /* SET MARK TO char */

/* Public wrappers for browse.c */
index_t *cmd_controlling_index(void);
int cmd_skip_deleted(const char *buf);
int cmd_check_filter(dbf_t *db);
void cmd_follow_relations(void);
int cmd_field_display_width(dbf_t *db, int f);
void cmd_indexes_capture_keys(dbf_t *db, char keys[][MAX_INDEX_KEY+1]);
int cmd_indexes_update_current(dbf_t *db, char keys[][MAX_INDEX_KEY+1]);
void cmd_indexes_insert_current(dbf_t *db);
void cmd_ctx_setup(void);
int cmd_get_deleted(void);
int cmd_get_softseek(void);
int cmd_get_unique(void);

/* SET ALTERNATE output wrappers */
FILE *cmd_get_alternate_fp(void);
int   cmd_get_alternate_on(void);
void  cmd_output(const char *s);
void  cmd_output_fmt(const char *fmt, ...);

/* SET options access */
const set_options_t *cmd_get_set_opts(void);
const char *cmd_get_path(void);

/* File search using SET PATH */
int cmd_resolve_file(const char *name, char *resolved, int size);

#endif
