#ifndef MENU_H
#define MENU_H

#define MAX_POPUPS 16
#define MAX_BARS   32

typedef struct {
    int number;           /* bar number (1-based, user-assigned) */
    char prompt[80];
    char message[160];
    int skip;             /* 1 = always skip */
    char skip_expr[256];  /* SKIP FOR expression (empty = no skip) */
} bar_entry_t;

typedef struct {
    char name[32];        /* uppercased */
    int from_row, from_col;
    int to_row, to_col;   /* 0,0 = auto-size */
    int defined;
    bar_entry_t bars[MAX_BARS];
    int nbar;
    char on_selection_proc[64]; /* ON SELECTION callback (empty = none) */
} popup_t;

/* Initialize popup subsystem */
void menu_init(void);

/* Define a popup menu. Returns pointer or NULL on error. */
popup_t *menu_define_popup(const char *name, int r1, int c1, int r2, int c2);

/* Define a bar within a named popup. Returns 0 on success, -1 on error. */
int menu_define_bar(const char *popup_name, int bar_num, const char *prompt,
                    const char *message, const char *skip_expr);

/* Activate (display and run lightbar) a named popup.
   Returns bar# or 0=Esc.  When from_menubar=1, Left/Right return -1/-2. */
int menu_activate_popup_ex(const char *name, int from_menubar);

/* Convenience wrapper: standalone popup (from_menubar=0). */
int menu_activate_popup(const char *name);

/* Deactivate the currently active popup (exit from ON SELECTION handler). */
void menu_deactivate_popup(void);

/* Release a named popup. */
void menu_release_popup(const char *name);

/* Release all popups. */
void menu_release_all(void);

/* Last selected bar number (0 if none). */
int menu_last_bar(void);

/* Last selected prompt text (empty string if none). */
const char *menu_last_prompt(void);

/* ---- Menu bar (horizontal) ---- */
#define MAX_MENUS 8
#define MAX_PADS  16

typedef struct {
    char name[32];        /* pad name (uppercased) */
    char prompt[80];
    char message[160];
    int row, col;         /* AT position (-1,-1 = auto-position) */
    char popup_name[32];  /* attached popup (empty = none) */
    char on_selection_proc[64]; /* ON SELECTION callback (empty = none) */
} pad_entry_t;

typedef struct {
    char name[32];        /* menu name (uppercased) */
    int defined;
    pad_entry_t pads[MAX_PADS];
    int npad;
} menu_t;

menu_t *menu_define_menu(const char *name);
int  menu_define_pad(const char *menu_name, const char *pad_name,
                     const char *prompt, int row, int col, const char *message);
int  menu_set_pad_popup(const char *menu_name, const char *pad_name,
                        const char *popup_name);
int  menu_activate_menu(const char *name);
void menu_deactivate_menu(void);
void menu_release_menu(const char *name);
void menu_release_all_menus(void);
const char *menu_last_pad(void);
const char *menu_last_popup(void);

/* ON SELECTION callbacks */
void menu_set_on_selection_popup(const char *popup_name, const char *procname);
void menu_set_on_selection_pad(const char *menu_name, const char *pad_name,
                               const char *procname);

#endif
