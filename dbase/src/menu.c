#include <stdio.h>
#include <string.h>
#include "menu.h"
#include "screen.h"
#include "command.h"
#include "expr.h"
#include "util.h"
#include "program.h"

#ifdef __slow32__
#include <term.h>
#define HAS_TERM 1
#else
#define HAS_TERM 0
#endif

/* Key codes — same as screen.c / browse.c */
#define DBASE_KEY_UP     5
#define DBASE_KEY_DOWN  24
#define DBASE_KEY_LEFT  19
#define DBASE_KEY_RIGHT  4
#define DBASE_KEY_HOME   1
#define DBASE_KEY_END    6

/* ---- Popup Registry ---- */
static popup_t popups[MAX_POPUPS];
static int last_bar_num;
static char last_prompt_text[80];
static int deactivate_flag;

/* ---- Menu Bar Registry ---- */
static menu_t menus[MAX_MENUS];
static char last_pad_name[32];
static char last_popup_name[32];
static int menu_deactivate_flag;

void menu_init(void) {
    int i;
    for (i = 0; i < MAX_POPUPS; i++)
        popups[i].defined = 0;
    for (i = 0; i < MAX_MENUS; i++)
        menus[i].defined = 0;
    last_bar_num = 0;
    last_prompt_text[0] = '\0';
    last_pad_name[0] = '\0';
    last_popup_name[0] = '\0';
    deactivate_flag = 0;
    menu_deactivate_flag = 0;
}

static popup_t *find_popup(const char *name) {
    char upper[32];
    int i;
    str_copy(upper, name, sizeof(upper));
    str_upper(upper);
    for (i = 0; i < MAX_POPUPS; i++) {
        if (popups[i].defined && str_icmp(popups[i].name, upper) == 0)
            return &popups[i];
    }
    return NULL;
}

static popup_t *alloc_popup(void) {
    int i;
    for (i = 0; i < MAX_POPUPS; i++) {
        if (!popups[i].defined)
            return &popups[i];
    }
    return NULL;
}

popup_t *menu_define_popup(const char *name, int r1, int c1, int r2, int c2) {
    popup_t *p = find_popup(name);
    if (!p) {
        p = alloc_popup();
        if (!p) {
            printf("Too many popups defined.\n");
            return NULL;
        }
    }
    str_copy(p->name, name, sizeof(p->name));
    str_upper(p->name);
    p->from_row = r1;
    p->from_col = c1;
    p->to_row = r2;
    p->to_col = c2;
    p->defined = 1;
    p->nbar = 0;
    p->on_selection_proc[0] = '\0';
    return p;
}

int menu_define_bar(const char *popup_name, int bar_num, const char *prompt,
                    const char *message, const char *skip_expr) {
    popup_t *p = find_popup(popup_name);
    if (!p) {
        printf("Popup not defined: %s\n", popup_name);
        return -1;
    }
    if (p->nbar >= MAX_BARS) {
        printf("Too many bars in popup %s.\n", p->name);
        return -1;
    }
    {
        bar_entry_t *b = &p->bars[p->nbar];
        b->number = bar_num;
        str_copy(b->prompt, prompt, sizeof(b->prompt));
        str_copy(b->message, message ? message : "", sizeof(b->message));
        b->skip = 0;
        str_copy(b->skip_expr, skip_expr ? skip_expr : "", sizeof(b->skip_expr));
        p->nbar++;
    }
    return 0;
}

/* Check if a bar should be skipped */
static int bar_is_skipped(const bar_entry_t *b) {
    if (b->skip) return 1;
    if (b->skip_expr[0]) {
        expr_ctx_t *ctx = cmd_get_expr_ctx();
        value_t result;
        if (expr_eval_str(ctx, b->skip_expr, &result) == 0 &&
            result.type == VAL_LOGIC && result.logic)
            return 1;
    }
    return 0;
}

/* Find first non-skipped bar index. Returns -1 if all skipped. */
static int first_selectable(const popup_t *p) {
    int i;
    for (i = 0; i < p->nbar; i++) {
        if (!bar_is_skipped(&p->bars[i]))
            return i;
    }
    return -1;
}

/* Navigate to next/previous non-skipped bar */
static int next_selectable(const popup_t *p, int cur, int dir, int wrap) {
    int i, n = p->nbar;
    for (i = 1; i <= n; i++) {
        int idx;
        if (dir > 0) {
            idx = (cur + i) % n;
            if (!wrap && cur + i >= n) return cur;
        } else {
            idx = (cur - i + n) % n;
            if (!wrap && cur - i < 0) return cur;
        }
        if (!bar_is_skipped(&p->bars[idx]))
            return idx;
    }
    return cur;
}

/* Find last non-skipped bar index */
static int last_selectable(const popup_t *p) {
    int i;
    for (i = p->nbar - 1; i >= 0; i--) {
        if (!bar_is_skipped(&p->bars[i]))
            return i;
    }
    return -1;
}

int menu_activate_popup_ex(const char *name, int from_menubar) {
    popup_t *p = find_popup(name);
    int cur, i;
    int msg_row, wrap;
    int r1, c1, r2, c2;
    int widest, inner_w;

    if (!p) {
        printf("Popup not defined: %s\n", name);
        return 0;
    }
    if (p->nbar == 0) {
        printf("Popup %s has no bars.\n", p->name);
        return 0;
    }

    cur = first_selectable(p);
    if (cur < 0) return 0;  /* all bars skipped */

    deactivate_flag = 0;
    msg_row = cmd_get_message_row();
    wrap = cmd_get_wrap();

    /* Compute bounds */
    r1 = p->from_row;
    c1 = p->from_col;
    widest = 0;
    for (i = 0; i < p->nbar; i++) {
        int len = (int)strlen(p->bars[i].prompt);
        if (len > widest) widest = len;
    }
    inner_w = widest + 2;  /* 1 space padding each side */

    if (p->to_row > 0 && p->to_col > 0) {
        r2 = p->to_row;
        c2 = p->to_col;
    } else {
        r2 = r1 + p->nbar + 1;  /* border top + nbar rows + border bottom */
        c2 = c1 + inner_w + 1;  /* border left + inner + border right */
    }

#if HAS_TERM
    if (screen_term_available()) {
        int key;

        /* Draw border */
        screen_box(r1, c1, r2, c2, 0);
        term_set_raw(1);

        for (;;) {
            /* Redraw all bars */
            term_begin_update();
            for (i = 0; i < p->nbar; i++) {
                int row = r1 + 1 + i;
                int j, plen;
                term_gotoxy(row, c1 + 1);
                if (i == cur)
                    term_set_attr(7);  /* reverse */
                else
                    term_set_attr(0);  /* normal */
                /* Space padding + prompt + space padding */
                term_putc(' ');
                term_puts(p->bars[i].prompt);
                plen = (int)strlen(p->bars[i].prompt);
                for (j = plen + 1; j < c2 - c1; j++)
                    term_putc(' ');
            }

            /* Show MESSAGE for current bar */
            if (msg_row >= 0) {
                char blank[81];
                int len = 80;
                if (len > (int)sizeof(blank) - 1) len = (int)sizeof(blank) - 1;
                memset(blank, ' ', len);
                blank[len] = '\0';
                term_gotoxy(msg_row, 0);
                term_set_attr(0);
                term_puts(blank);
                if (p->bars[cur].message[0]) {
                    int mlen = (int)strlen(p->bars[cur].message);
                    int mcol = (80 - mlen) / 2;
                    if (mcol < 0) mcol = 0;
                    term_gotoxy(msg_row, mcol);
                    term_puts(p->bars[cur].message);
                }
            }
            term_end_update();

            key = read_dbase_key();
            if (screen_check_key_handler(key)) continue;

            if (deactivate_flag) {
                cur = -1;
                break;
            }

            if (key == '\r' || key == '\n') {
                screen_set_lastkey(13);
                /* Store result before callback so BAR()/PROMPT() work */
                last_bar_num = p->bars[cur].number;
                str_copy(last_prompt_text, p->bars[cur].prompt,
                         sizeof(last_prompt_text));
                if (p->on_selection_proc[0]) {
                    term_set_raw(0);
                    prog_call_sync(p->on_selection_proc);
                    term_set_raw(1);
                    if (deactivate_flag) { cur = -1; break; }
                    continue;  /* loop back to redraw */
                }
                break;
            }
            if (key == 27) {  /* Escape */
                screen_set_lastkey(27);
                cur = -1;
                break;
            }
            if (key == DBASE_KEY_UP) {
                cur = next_selectable(p, cur, -1, wrap);
            } else if (key == DBASE_KEY_DOWN) {
                cur = next_selectable(p, cur, 1, wrap);
            } else if (key == DBASE_KEY_HOME) {
                int f = first_selectable(p);
                if (f >= 0) cur = f;
            } else if (key == DBASE_KEY_END) {
                int l = last_selectable(p);
                if (l >= 0) cur = l;
            } else if (from_menubar && key == DBASE_KEY_LEFT) {
                /* Close popup, return -1 = move left in menu bar */
                term_set_attr(0);
                if (msg_row >= 0) {
                    char blank[81];
                    int len = 80;
                    memset(blank, ' ', len);
                    blank[len] = '\0';
                    term_gotoxy(msg_row, 0);
                    term_puts(blank);
                }
                term_set_raw(0);
                screen_clear_region(r1, c1, r2, c2);
                return -1;
            } else if (from_menubar && key == DBASE_KEY_RIGHT) {
                /* Close popup, return -2 = move right in menu bar */
                term_set_attr(0);
                if (msg_row >= 0) {
                    char blank[81];
                    int len = 80;
                    memset(blank, ' ', len);
                    blank[len] = '\0';
                    term_gotoxy(msg_row, 0);
                    term_puts(blank);
                }
                term_set_raw(0);
                screen_clear_region(r1, c1, r2, c2);
                return -2;
            }
        }

        /* Restore display */
        term_set_attr(0);

        /* Clear message row */
        if (msg_row >= 0) {
            char blank[81];
            int len = 80;
            if (len > (int)sizeof(blank) - 1) len = (int)sizeof(blank) - 1;
            memset(blank, ' ', len);
            blank[len] = '\0';
            term_gotoxy(msg_row, 0);
            term_puts(blank);
        }

        term_set_raw(0);

        /* Clear popup region */
        screen_clear_region(r1, c1, r2, c2);
    } else
#endif
    {
        /* Non-terminal fallback: print bars, pick first non-skipped */
        printf("Popup %s:\n", p->name);
        for (i = 0; i < p->nbar; i++) {
            if (bar_is_skipped(&p->bars[i]))
                printf("  %d. %s (skipped)\n", p->bars[i].number, p->bars[i].prompt);
            else
                printf("  %d. %s\n", p->bars[i].number, p->bars[i].prompt);
        }
    }

    /* Store result */
    if (cur >= 0) {
        last_bar_num = p->bars[cur].number;
        str_copy(last_prompt_text, p->bars[cur].prompt, sizeof(last_prompt_text));
        /* Non-terminal callback: fire once and return */
        if (p->on_selection_proc[0]) {
            prog_call_sync(p->on_selection_proc);
        }
    } else {
        last_bar_num = 0;
        last_prompt_text[0] = '\0';
    }

    return last_bar_num;
}

int menu_activate_popup(const char *name) {
    return menu_activate_popup_ex(name, 0);
}

void menu_deactivate_popup(void) {
    deactivate_flag = 1;
}

void menu_release_popup(const char *name) {
    popup_t *p = find_popup(name);
    if (p) {
        p->defined = 0;
    } else {
        printf("Popup not defined: %s\n", name);
    }
}

void menu_release_all(void) {
    int i;
    for (i = 0; i < MAX_POPUPS; i++)
        popups[i].defined = 0;
}

int menu_last_bar(void) {
    return last_bar_num;
}

const char *menu_last_prompt(void) {
    return last_prompt_text;
}

/* ================================================================ */
/* ---- Menu Bar (horizontal) implementation ---- */
/* ================================================================ */

static menu_t *find_menu(const char *name) {
    char upper[32];
    int i;
    str_copy(upper, name, sizeof(upper));
    str_upper(upper);
    for (i = 0; i < MAX_MENUS; i++) {
        if (menus[i].defined && str_icmp(menus[i].name, upper) == 0)
            return &menus[i];
    }
    return NULL;
}

static menu_t *alloc_menu(void) {
    int i;
    for (i = 0; i < MAX_MENUS; i++) {
        if (!menus[i].defined)
            return &menus[i];
    }
    return NULL;
}

menu_t *menu_define_menu(const char *name) {
    menu_t *m = find_menu(name);
    if (!m) {
        m = alloc_menu();
        if (!m) {
            printf("Too many menus defined.\n");
            return NULL;
        }
    }
    str_copy(m->name, name, sizeof(m->name));
    str_upper(m->name);
    m->defined = 1;
    m->npad = 0;
    return m;
}

int menu_define_pad(const char *menu_name, const char *pad_name,
                    const char *prompt, int row, int col, const char *message) {
    menu_t *m = find_menu(menu_name);
    pad_entry_t *pad;
    if (!m) {
        printf("Menu not defined: %s\n", menu_name);
        return -1;
    }
    if (m->npad >= MAX_PADS) {
        printf("Too many pads in menu %s.\n", m->name);
        return -1;
    }
    pad = &m->pads[m->npad];
    str_copy(pad->name, pad_name, sizeof(pad->name));
    str_upper(pad->name);
    str_copy(pad->prompt, prompt, sizeof(pad->prompt));
    str_copy(pad->message, message ? message : "", sizeof(pad->message));
    pad->row = row;
    pad->col = col;
    pad->popup_name[0] = '\0';
    pad->on_selection_proc[0] = '\0';
    m->npad++;
    return 0;
}

int menu_set_pad_popup(const char *menu_name, const char *pad_name,
                       const char *popup_name) {
    menu_t *m = find_menu(menu_name);
    char upper[32];
    int i;
    if (!m) {
        printf("Menu not defined: %s\n", menu_name);
        return -1;
    }
    str_copy(upper, pad_name, sizeof(upper));
    str_upper(upper);
    for (i = 0; i < m->npad; i++) {
        if (str_icmp(m->pads[i].name, upper) == 0) {
            str_copy(m->pads[i].popup_name, popup_name,
                     sizeof(m->pads[i].popup_name));
            str_upper(m->pads[i].popup_name);
            return 0;
        }
    }
    printf("Pad not defined: %s OF %s\n", pad_name, m->name);
    return -1;
}

/* Compute auto-positions for pads that have row/col == -1 */
static void compute_pad_positions(menu_t *m) {
    int next_col = 0;
    int i;
    for (i = 0; i < m->npad; i++) {
        if (m->pads[i].row < 0 || m->pads[i].col < 0) {
            m->pads[i].row = 0;
            m->pads[i].col = next_col;
        }
        next_col = m->pads[i].col + (int)strlen(m->pads[i].prompt) + 2;
    }
}

int menu_activate_menu(const char *name) {
    menu_t *m = find_menu(name);
    int cur, i;
    int msg_row;

    if (!m) {
        printf("Menu not defined: %s\n", name);
        return 0;
    }
    if (m->npad == 0) {
        printf("Menu %s has no pads.\n", m->name);
        return 0;
    }

    compute_pad_positions(m);
    menu_deactivate_flag = 0;
    cur = 0;
    msg_row = cmd_get_message_row();

#if HAS_TERM
    if (screen_term_available()) {
        int key;
        term_set_raw(1);

        for (;;) {
            /* Draw pad bar */
            term_begin_update();
            for (i = 0; i < m->npad; i++) {
                term_gotoxy(m->pads[i].row, m->pads[i].col);
                if (i == cur)
                    term_set_attr(7);  /* reverse */
                else
                    term_set_attr(0);
                term_putc(' ');
                term_puts(m->pads[i].prompt);
                term_putc(' ');
            }
            term_set_attr(0);

            /* Show MESSAGE for current pad */
            if (msg_row >= 0) {
                char blank[81];
                int len = 80;
                memset(blank, ' ', len);
                blank[len] = '\0';
                term_gotoxy(msg_row, 0);
                term_puts(blank);
                if (m->pads[cur].message[0]) {
                    int mlen = (int)strlen(m->pads[cur].message);
                    int mcol = (80 - mlen) / 2;
                    if (mcol < 0) mcol = 0;
                    term_gotoxy(msg_row, mcol);
                    term_puts(m->pads[cur].message);
                }
            }
            term_end_update();

            key = read_dbase_key();
            if (screen_check_key_handler(key)) continue;

            if (menu_deactivate_flag) break;

            if (key == 27) {  /* Escape */
                screen_set_lastkey(27);
                break;
            }
            if (key == DBASE_KEY_LEFT) {
                cur = (cur - 1 + m->npad) % m->npad;
            } else if (key == DBASE_KEY_RIGHT) {
                cur = (cur + 1) % m->npad;
            } else if (key == '\r' || key == '\n' || key == DBASE_KEY_DOWN) {
                /* Open attached popup */
                if (m->pads[cur].popup_name[0]) {
                    int result;
                    term_set_raw(0);
                    result = menu_activate_popup_ex(m->pads[cur].popup_name, 1);
                    term_set_raw(1);
                    if (result > 0) {
                        /* Bar selected — store pad/popup */
                        str_copy(last_pad_name, m->pads[cur].name,
                                 sizeof(last_pad_name));
                        str_copy(last_popup_name, m->pads[cur].popup_name,
                                 sizeof(last_popup_name));
                        if (m->pads[cur].on_selection_proc[0]) {
                            term_set_raw(0);
                            prog_call_sync(m->pads[cur].on_selection_proc);
                            term_set_raw(1);
                            if (menu_deactivate_flag) goto done_term;
                            continue;  /* re-open popup */
                        }
                        goto done_term;
                    } else if (result == -1) {
                        /* Left from popup — move to previous pad and open */
                        cur = (cur - 1 + m->npad) % m->npad;
                        if (m->pads[cur].popup_name[0]) {
                            for (;;) {
                                term_set_raw(0);
                                /* Redraw pad bar before opening next popup */
                                {
                                    int j;
                                    for (j = 0; j < m->npad; j++) {
                                        term_gotoxy(m->pads[j].row, m->pads[j].col);
                                        if (j == cur)
                                            term_set_attr(7);
                                        else
                                            term_set_attr(0);
                                        term_putc(' ');
                                        term_puts(m->pads[j].prompt);
                                        term_putc(' ');
                                    }
                                    term_set_attr(0);
                                }
                                result = menu_activate_popup_ex(
                                    m->pads[cur].popup_name, 1);
                                term_set_raw(1);
                                if (result > 0) {
                                    str_copy(last_pad_name, m->pads[cur].name,
                                             sizeof(last_pad_name));
                                    str_copy(last_popup_name,
                                             m->pads[cur].popup_name,
                                             sizeof(last_popup_name));
                                    if (m->pads[cur].on_selection_proc[0]) {
                                        term_set_raw(0);
                                        prog_call_sync(m->pads[cur].on_selection_proc);
                                        term_set_raw(1);
                                        if (menu_deactivate_flag) goto done_term;
                                        continue;
                                    }
                                    goto done_term;
                                } else if (result == 0) {
                                    break;  /* Esc — back to pad nav */
                                } else if (result == -1) {
                                    cur = (cur - 1 + m->npad) % m->npad;
                                    if (!m->pads[cur].popup_name[0]) break;
                                } else if (result == -2) {
                                    cur = (cur + 1) % m->npad;
                                    if (!m->pads[cur].popup_name[0]) break;
                                }
                            }
                        }
                    } else if (result == -2) {
                        /* Right from popup — move to next pad and open */
                        cur = (cur + 1) % m->npad;
                        if (m->pads[cur].popup_name[0]) {
                            for (;;) {
                                term_set_raw(0);
                                {
                                    int j;
                                    for (j = 0; j < m->npad; j++) {
                                        term_gotoxy(m->pads[j].row, m->pads[j].col);
                                        if (j == cur)
                                            term_set_attr(7);
                                        else
                                            term_set_attr(0);
                                        term_putc(' ');
                                        term_puts(m->pads[j].prompt);
                                        term_putc(' ');
                                    }
                                    term_set_attr(0);
                                }
                                result = menu_activate_popup_ex(
                                    m->pads[cur].popup_name, 1);
                                term_set_raw(1);
                                if (result > 0) {
                                    str_copy(last_pad_name, m->pads[cur].name,
                                             sizeof(last_pad_name));
                                    str_copy(last_popup_name,
                                             m->pads[cur].popup_name,
                                             sizeof(last_popup_name));
                                    if (m->pads[cur].on_selection_proc[0]) {
                                        term_set_raw(0);
                                        prog_call_sync(m->pads[cur].on_selection_proc);
                                        term_set_raw(1);
                                        if (menu_deactivate_flag) goto done_term;
                                        continue;
                                    }
                                    goto done_term;
                                } else if (result == 0) {
                                    break;
                                } else if (result == -1) {
                                    cur = (cur - 1 + m->npad) % m->npad;
                                    if (!m->pads[cur].popup_name[0]) break;
                                } else if (result == -2) {
                                    cur = (cur + 1) % m->npad;
                                    if (!m->pads[cur].popup_name[0]) break;
                                }
                            }
                        }
                    }
                    /* result == 0 (Esc from popup) → back to pad nav */
                }
            }
        }

done_term:
        /* Restore */
        term_set_attr(0);
        if (msg_row >= 0) {
            char blank[81];
            int len = 80;
            memset(blank, ' ', len);
            blank[len] = '\0';
            term_gotoxy(msg_row, 0);
            term_puts(blank);
        }
        /* Clear pad bar region */
        for (i = 0; i < m->npad; i++) {
            int plen = (int)strlen(m->pads[i].prompt) + 2;
            int j;
            term_gotoxy(m->pads[i].row, m->pads[i].col);
            for (j = 0; j < plen; j++)
                term_putc(' ');
        }
        term_set_raw(0);
    } else
#endif
    {
        /* Non-terminal fallback: print pads, pick first, open its popup */
        printf("Menu %s:\n", m->name);
        for (i = 0; i < m->npad; i++) {
            printf("  %s", m->pads[i].prompt);
            if (m->pads[i].popup_name[0])
                printf(" -> %s", m->pads[i].popup_name);
            printf("\n");
        }
        /* Auto-select first pad */
        str_copy(last_pad_name, m->pads[0].name, sizeof(last_pad_name));
        if (m->pads[0].popup_name[0]) {
            str_copy(last_popup_name, m->pads[0].popup_name,
                     sizeof(last_popup_name));
            menu_activate_popup(m->pads[0].popup_name);
            if (m->pads[0].on_selection_proc[0]) {
                prog_call_sync(m->pads[0].on_selection_proc);
            }
        } else {
            last_popup_name[0] = '\0';
        }
    }

    return 0;
}

void menu_deactivate_menu(void) {
    menu_deactivate_flag = 1;
    /* Also deactivate any active popup */
    menu_deactivate_popup();
}

void menu_release_menu(const char *name) {
    menu_t *m = find_menu(name);
    if (m) {
        m->defined = 0;
    } else {
        printf("Menu not defined: %s\n", name);
    }
}

void menu_release_all_menus(void) {
    int i;
    for (i = 0; i < MAX_MENUS; i++)
        menus[i].defined = 0;
}

const char *menu_last_pad(void) {
    return last_pad_name;
}

const char *menu_last_popup(void) {
    return last_popup_name;
}

void menu_set_on_selection_popup(const char *popup_name, const char *procname) {
    popup_t *p = find_popup(popup_name);
    if (!p) {
        printf("Popup not defined: %s\n", popup_name);
        return;
    }
    str_copy(p->on_selection_proc, procname, sizeof(p->on_selection_proc));
}

void menu_set_on_selection_pad(const char *menu_name, const char *pad_name,
                               const char *procname) {
    menu_t *m = find_menu(menu_name);
    char upper[32];
    int i;
    if (!m) {
        printf("Menu not defined: %s\n", menu_name);
        return;
    }
    str_copy(upper, pad_name, sizeof(upper));
    str_upper(upper);
    for (i = 0; i < m->npad; i++) {
        if (str_icmp(m->pads[i].name, upper) == 0) {
            str_copy(m->pads[i].on_selection_proc, procname,
                     sizeof(m->pads[i].on_selection_proc));
            return;
        }
    }
    printf("Pad not defined: %s OF %s\n", pad_name, m->name);
}
