#ifndef SCREEN_H
#define SCREEN_H

#include "expr.h"
#include "memvar.h"

struct ast_node;

#define MAX_GETS 64
#define MAX_PROMPTS 20

typedef struct {
    int row, col;
    char varname[MEMVAR_NAMELEN];
    char picture[64];
    int width;
    val_type_t type;
    char initial[256];
    int has_range;
    value_t range_lo, range_hi;
    char valid_expr[256];
    char when_expr[256];
    struct ast_node *valid_ast;
    struct ast_node *when_ast;
    int is_field;    /* 1 if variable is a database field */
    int field_index; /* field index in db (valid when is_field=1) */
} get_entry_t;

typedef struct {
    int row, col;
    char text[80];
    char message[160];
} prompt_entry_t;

typedef struct {
    get_entry_t gets[MAX_GETS];
    int ngets;
    prompt_entry_t prompts[MAX_PROMPTS];
    int nprompts;
    int term_available;
    int fg_color, bg_color;
    int last_row, last_col;  /* cursor position tracking */
} screen_state_t;

/* Initialize screen subsystem */
void screen_init(void);

/* Shutdown screen subsystem (release terminal service) */
void screen_shutdown(void);

/* @SAY command */
void screen_say(int row, int col, const char *expr_str, const char *picture);

/* @GET command (range_lo/range_hi = NULL if no RANGE) */
void screen_get(int row, int col, const char *varname, const char *picture,
                const value_t *range_lo, const value_t *range_hi,
                const char *valid_expr, const char *when_expr);

/* @CLEAR TO command */
void screen_clear_region(int r1, int c1, int r2, int c2);

/* @TO command (box drawing) */
void screen_box(int r1, int c1, int r2, int c2, int dbl);

/* READ command */
void screen_read(void);

/* CLEAR command */
void screen_clear(void);

/* CLEAR GETS command */
void screen_clear_gets(void);

/* @PROMPT command */
void screen_prompt(int row, int col, const char *text, const char *message);

/* MENU TO command — returns 1-based selection (0 if Esc) */
int screen_menu_to(void);

/* CLEAR prompts */
void screen_clear_prompts(void);

/* SET COLOR TO command */
void screen_set_color(const char *spec);

/* Parse and execute @ command */
void screen_at_cmd(const char *line);

/* Cursor position tracking (for ROW()/COL() functions) */
int screen_get_row(void);
int screen_get_col(void);

/* Printer position tracking (for PROW()/PCOL() functions) */
int screen_get_prow(void);
int screen_get_pcol(void);

/* EJECT command (form feed / reset printer position) */
void screen_eject(void);

/* Printer output helpers that track PROW()/PCOL() */
void screen_print_newline(void);
void screen_print_text(const char *s);

/* Keyboard functions */
int  screen_inkey(double timeout);  /* INKEY(): <0=wait forever, 0=poll, >0=timeout secs */
int  screen_lastkey(void);          /* LASTKEY(): last key from INKEY/READ/WAIT */
int  screen_readkey(void);          /* READKEY(): how user exited last READ */
void screen_set_lastkey(int key);   /* Set lastkey (for WAIT, ACCEPT, etc.) */

/* Check for Esc keypress (non-blocking). Returns 1 if Esc pressed, 0 otherwise. */
int screen_check_escape(void);

/* Low-level key reader: reads one keypress from terminal, decoding escape sequences.
   Returns dBase key code. Only available when HAS_TERM is 1. */
int read_dbase_key(void);

/* SAVE SCREEN / RESTORE SCREEN */
void screen_save(void);
void screen_restore(void);

/* Check if terminal service is available */
int screen_term_available(void);

/* Key handler support (SET KEY / ON KEY) */
void screen_set_key_handler(int keycode, const char *procname);
void screen_clear_key_handlers(void);
int  screen_check_key_handler(int keycode);  /* returns 1 if handler fired */

#endif
