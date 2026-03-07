/*
 * nano.c — A nano-like text editor for SLOW-32
 *
 * Uses the terminal service (term.h) for full-screen editing.
 * Single-file implementation with array-of-lines buffer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>

/* ---- Constants ---- */

#define TAB_STOP       4
#define LINE_INIT_CAP  64
#define LINES_INIT_CAP 128
#define MAX_FILENAME   256
#define MAX_SEARCH     256
#define MAX_MESSAGE    256
#define MAX_DIRTY      256  /* max screen rows for dirty tracking */
#define QUIT_CONFIRM   2    /* presses of Ctrl-Q to force-quit when modified */

/* Internal key codes (above ASCII range) */
#define KEY_NONE    0
#define KEY_UP      1000
#define KEY_DOWN    1001
#define KEY_LEFT    1002
#define KEY_RIGHT   1003
#define KEY_HOME    1004
#define KEY_END     1005
#define KEY_PGUP    1006
#define KEY_PGDN    1007
#define KEY_DEL     1008
#define KEY_CTRL_LEFT  1009
#define KEY_CTRL_RIGHT 1010

/* Ctrl key helper */
#define CTRL(k) ((k) & 0x1f)

/* ---- Data Structures ---- */

typedef struct {
    char *text;
    int len;
    int cap;
} line_t;

typedef struct {
    line_t *lines;
    int num_lines;
    int lines_cap;
    int cx, cy;                 /* cursor position (0-based, in file coords) */
    int row_offset, col_offset; /* viewport scroll offsets */
    int screen_rows, screen_cols;
    int edit_rows;              /* rows available for editing (screen - 2) */
    int gutter_width;           /* line number gutter width */
    char filename[MAX_FILENAME];
    int modified;
    int running;
    int quit_count;             /* consecutive Ctrl-Q presses */
    char search_buf[MAX_SEARCH];
    line_t cut_line;            /* cut buffer (single line) */
    int has_cut;
    char message[MAX_MESSAGE];
    char dirty[MAX_DIRTY];      /* 1 = screen row needs redraw */
    int dirty_status;           /* 1 = status bar needs redraw */
    int dirty_bottom;           /* 1 = message/help bar needs redraw */
    int prev_row_offset;        /* detect scroll changes */
    int prev_col_offset;
    int prev_gutter_width;
    int prev_num_lines;         /* detect line count changes (gutter width) */
    char prev_message[MAX_MESSAGE];
} editor_t;

static editor_t E;

/* Forward declarations */
static void editor_set_message(const char *msg);
static void editor_prompt(const char *prompt, char *buf, int bufsize);

/* ---- Line Memory Management ---- */

static void line_init(line_t *l) {
    l->cap = LINE_INIT_CAP;
    l->text = malloc(l->cap);
    l->text[0] = '\0';
    l->len = 0;
}

static void line_free(line_t *l) {
    free(l->text);
    l->text = NULL;
    l->len = 0;
    l->cap = 0;
}

static void line_ensure_cap(line_t *l, int needed) {
    if (needed <= l->cap) return;
    while (l->cap < needed) l->cap *= 2;
    l->text = realloc(l->text, l->cap);
}

static void line_set(line_t *l, const char *s, int slen) {
    line_ensure_cap(l, slen + 1);
    memcpy(l->text, s, slen);
    l->text[slen] = '\0';
    l->len = slen;
}

static void line_insert_char(line_t *l, int pos, char ch) {
    if (pos < 0) pos = 0;
    if (pos > l->len) pos = l->len;
    line_ensure_cap(l, l->len + 2);
    memmove(l->text + pos + 1, l->text + pos, l->len - pos + 1);
    l->text[pos] = ch;
    l->len++;
}

static void line_delete_char(line_t *l, int pos) {
    if (pos < 0 || pos >= l->len) return;
    memmove(l->text + pos, l->text + pos + 1, l->len - pos);
    l->len--;
}

static void line_append(line_t *l, const char *s, int slen) {
    line_ensure_cap(l, l->len + slen + 1);
    memcpy(l->text + l->len, s, slen);
    l->len += slen;
    l->text[l->len] = '\0';
}

/* ---- Editor Line Array Operations ---- */

static void editor_ensure_lines_cap(int needed) {
    if (needed <= E.lines_cap) return;
    while (E.lines_cap < needed) E.lines_cap *= 2;
    E.lines = realloc(E.lines, E.lines_cap * sizeof(line_t));
}

static void editor_insert_line(int at, const char *s, int slen) {
    if (at < 0) at = 0;
    if (at > E.num_lines) at = E.num_lines;
    editor_ensure_lines_cap(E.num_lines + 1);
    if (at < E.num_lines) {
        memmove(&E.lines[at + 1], &E.lines[at],
                (E.num_lines - at) * sizeof(line_t));
    }
    line_init(&E.lines[at]);
    line_set(&E.lines[at], s, slen);
    E.num_lines++;
}

static void editor_delete_line(int at) {
    if (at < 0 || at >= E.num_lines) return;
    line_free(&E.lines[at]);
    if (at < E.num_lines - 1) {
        memmove(&E.lines[at], &E.lines[at + 1],
                (E.num_lines - at - 1) * sizeof(line_t));
    }
    E.num_lines--;
}

/* Split line at position: line[at] becomes two lines */
static void editor_split_line(int at, int pos) {
    line_t *l = &E.lines[at];
    int tail_len = l->len - pos;
    char *tail = l->text + pos;
    editor_insert_line(at + 1, tail, tail_len);
    /* Truncate original line */
    l->text[pos] = '\0';
    l->len = pos;
}

/* Join line[at] with line[at+1] */
static void editor_join_lines(int at) {
    if (at < 0 || at >= E.num_lines - 1) return;
    line_append(&E.lines[at], E.lines[at + 1].text, E.lines[at + 1].len);
    editor_delete_line(at + 1);
}

/* ---- Gutter Width ---- */

static int compute_gutter_width(void) {
    int n = E.num_lines;
    int digits = 1;
    while (n >= 10) { digits++; n /= 10; }
    return digits + 1; /* digits + space */
}

/* ---- Dirty Tracking ---- */

/* Mark all screen rows dirty (e.g. after scroll) */
static void dirty_all(void) {
    int i;
    for (i = 0; i < E.edit_rows && i < MAX_DIRTY; i++)
        E.dirty[i] = 1;
    E.dirty_status = 1;
    E.dirty_bottom = 1;
}

/* Mark a single file row dirty (translates to screen row) */
static void dirty_file_row(int file_row) {
    int screen_row = file_row - E.row_offset;
    if (screen_row >= 0 && screen_row < E.edit_rows && screen_row < MAX_DIRTY)
        E.dirty[screen_row] = 1;
}

/* Mark file row and all rows below it dirty (for insert/delete line) */
static void dirty_from_file_row(int file_row) {
    int screen_row = file_row - E.row_offset;
    int i;
    if (screen_row < 0) screen_row = 0;
    for (i = screen_row; i < E.edit_rows && i < MAX_DIRTY; i++)
        E.dirty[i] = 1;
}

/* ---- File I/O ---- */

static void editor_load(const char *filename) {
    FILE *f;
    char buf[4096];
    int buf_len = 0;
    int ch;

    strncpy(E.filename, filename, MAX_FILENAME - 1);
    E.filename[MAX_FILENAME - 1] = '\0';

    f = fopen(filename, "r");
    if (!f) {
        /* New file */
        editor_insert_line(0, "", 0);
        snprintf(E.message, MAX_MESSAGE, "New file: %s", filename);
        return;
    }

    while ((ch = fgetc(f)) != EOF) {
        if (ch == '\r') continue; /* strip CR */
        if (ch == '\n') {
            editor_insert_line(E.num_lines, buf, buf_len);
            buf_len = 0;
            continue;
        }
        if (ch == '\t') {
            /* Expand tab to spaces */
            int spaces = TAB_STOP - (buf_len % TAB_STOP);
            int i;
            for (i = 0; i < spaces && buf_len < (int)sizeof(buf) - 1; i++)
                buf[buf_len++] = ' ';
            continue;
        }
        if (buf_len < (int)sizeof(buf) - 1)
            buf[buf_len++] = (char)ch;
    }
    /* Last line (may not end with newline) */
    if (buf_len > 0 || E.num_lines == 0) {
        editor_insert_line(E.num_lines, buf, buf_len);
    }
    fclose(f);

    /* Ensure at least one line */
    if (E.num_lines == 0)
        editor_insert_line(0, "", 0);

    snprintf(E.message, MAX_MESSAGE, "Loaded %s (%d lines)", filename, E.num_lines);
}

static int editor_save(void) {
    FILE *f;
    int i;

    if (E.filename[0] == '\0') {
        char namebuf[MAX_FILENAME] = "";
        editor_prompt("Filename: ", namebuf, MAX_FILENAME);
        if (namebuf[0] == '\0') {
            editor_set_message("Save cancelled");
            return -1;
        }
        strncpy(E.filename, namebuf, MAX_FILENAME - 1);
        E.filename[MAX_FILENAME - 1] = '\0';
    }

    f = fopen(E.filename, "w");
    if (!f) {
        snprintf(E.message, MAX_MESSAGE, "Can't save: %s", E.filename);
        return -1;
    }

    for (i = 0; i < E.num_lines; i++) {
        fwrite(E.lines[i].text, 1, E.lines[i].len, f);
        fputc('\n', f);
    }
    fclose(f);

    E.modified = 0;
    snprintf(E.message, MAX_MESSAGE, "Saved %s (%d lines)", E.filename, E.num_lines);
    return 0;
}

/* ---- Keyboard Input ---- */

static int read_key(void) {
    int ch = term_getkey();
    if (ch < 0) return -1;

    if (ch == 27) {
        /* Escape sequence */
        if (!term_kbhit()) return 27;
        int ch2 = term_getkey();
        if (ch2 == '[') {
            int ch3 = term_getkey();
            switch (ch3) {
            case 'A': return KEY_UP;
            case 'B': return KEY_DOWN;
            case 'C': return KEY_RIGHT;
            case 'D': return KEY_LEFT;
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
            case '1':
                if (term_kbhit()) {
                    int ch4 = term_getkey();
                    if (ch4 == '~') return KEY_HOME;
                    if (ch4 == ';' && term_kbhit()) {
                        int ch5 = term_getkey();
                        if (ch5 == '5' && term_kbhit()) {
                            int ch6 = term_getkey();
                            if (ch6 == 'D') return KEY_CTRL_LEFT;
                            if (ch6 == 'C') return KEY_CTRL_RIGHT;
                        }
                    }
                }
                return KEY_HOME;
            case '4':
                if (term_kbhit()) term_getkey(); /* consume ~ */
                return KEY_END;
            case '5':
                if (term_kbhit()) term_getkey();
                return KEY_PGUP;
            case '6':
                if (term_kbhit()) term_getkey();
                return KEY_PGDN;
            case '3':
                if (term_kbhit()) term_getkey();
                return KEY_DEL;
            default:
                return ch3;
            }
        }
        return ch2;
    }

    /* Normalize backspace */
    if (ch == 127) return 8;

    return ch;
}

/* ---- Screen Rendering ---- */

static void editor_set_message(const char *msg) {
    strncpy(E.message, msg, MAX_MESSAGE - 1);
    E.message[MAX_MESSAGE - 1] = '\0';
}

/* Draw a single row of the editor (text content area) */
static void editor_draw_row(int screen_row, int file_row) {
    /* Position cursor at start of row (1-based) */
    term_gotoxy(screen_row, 1);

    if (file_row >= E.num_lines) {
        /* Beyond end of file: draw tilde + clear */
        term_putc('~');
        term_clear(1);
        return;
    }

    /* Draw line number gutter */
    {
        char gutter[16];
        int g;
        snprintf(gutter, sizeof(gutter), "%*d ", E.gutter_width - 1, file_row + 1);
        for (g = 0; gutter[g]; g++)
            term_putc(gutter[g]);
    }

    /* Draw text content (with horizontal scroll) */
    {
        line_t *l = &E.lines[file_row];
        int text_cols = E.screen_cols - E.gutter_width;
        int j;
        for (j = 0; j < text_cols; j++) {
            int file_col = E.col_offset + j;
            if (file_col < l->len)
                term_putc(l->text[file_col]);
            else
                break;
        }
    }
    term_clear(1); /* clear rest of line */
}

static void editor_draw_rows(void) {
    int y;
    for (y = 0; y < E.edit_rows; y++) {
        int file_row = E.row_offset + y;
        editor_draw_row(y + 1, file_row);
    }
}

static void editor_draw_status_bar(void) {
    char left[128], right[64];
    int left_len, right_len;
    int i, pad;

    snprintf(left, sizeof(left), " %.40s%s",
             E.filename[0] ? E.filename : "[No Name]",
             E.modified ? " [Modified]" : "");
    snprintf(right, sizeof(right), "Ln %d, Col %d  %d lines",
             E.cy + 1, E.cx + 1, E.num_lines);

    left_len = strlen(left);
    right_len = strlen(right);

    term_gotoxy(E.screen_rows - 1, 1);
    term_set_attr(7); /* reverse video */

    /* Left-aligned info */
    term_puts(left);

    /* Pad between left and right */
    pad = E.screen_cols - left_len - right_len;
    if (pad < 1) pad = 1;
    for (i = 0; i < pad; i++)
        term_putc(' ');

    /* Right-aligned info */
    if (left_len + right_len < E.screen_cols)
        term_puts(right);

    term_set_attr(0); /* normal */
}

static void editor_draw_help_bar(void) {
    const char *help = " ^S Save  ^Q Quit  ^F Find  ^R Replace  ^K Cut  ^U Paste  ^D Dup";
    int len, i;

    term_gotoxy(E.screen_rows, 1);
    term_set_attr(7);

    len = strlen(help);
    term_puts(help);
    for (i = len; i < E.screen_cols; i++)
        term_putc(' ');

    term_set_attr(0);
}

static void editor_scroll(void) {
    /* Vertical scroll */
    if (E.cy < E.row_offset)
        E.row_offset = E.cy;
    if (E.cy >= E.row_offset + E.edit_rows)
        E.row_offset = E.cy - E.edit_rows + 1;

    /* Horizontal scroll */
    {
        int text_cols = E.screen_cols - E.gutter_width;
        if (E.cx < E.col_offset)
            E.col_offset = E.cx;
        if (E.cx >= E.col_offset + text_cols)
            E.col_offset = E.cx - text_cols + 1;
    }
}

static void editor_refresh_screen(void) {
    int y;
    int new_gutter = compute_gutter_width();

    editor_scroll();

    /* Detect changes that require full redraw */
    if (E.row_offset != E.prev_row_offset ||
        E.col_offset != E.prev_col_offset ||
        new_gutter != E.prev_gutter_width) {
        dirty_all();
    }
    E.prev_row_offset = E.row_offset;
    E.prev_col_offset = E.col_offset;
    E.prev_gutter_width = new_gutter;
    E.gutter_width = new_gutter;

    /* Detect status bar changes */
    if (E.num_lines != E.prev_num_lines) {
        E.dirty_status = 1;
        E.prev_num_lines = E.num_lines;
    }

    /* Detect message bar changes */
    if (strcmp(E.message, E.prev_message) != 0) {
        E.dirty_bottom = 1;
        strncpy(E.prev_message, E.message, MAX_MESSAGE - 1);
        E.prev_message[MAX_MESSAGE - 1] = '\0';
    }

    term_begin_update();

    /* Only redraw dirty rows */
    for (y = 0; y < E.edit_rows; y++) {
        if (y < MAX_DIRTY && E.dirty[y]) {
            int file_row = E.row_offset + y;
            editor_draw_row(y + 1, file_row);
            E.dirty[y] = 0;
        }
    }

    if (E.dirty_status) {
        editor_draw_status_bar();
        E.dirty_status = 0;
    }

    if (E.dirty_bottom) {
        /* Message bar or help bar */
        if (E.message[0]) {
            term_gotoxy(E.screen_rows, 1);
            term_set_attr(7);
            {
                int len = strlen(E.message);
                int i;
                term_puts(E.message);
                for (i = len; i < E.screen_cols; i++)
                    term_putc(' ');
            }
            term_set_attr(0);
        } else {
            editor_draw_help_bar();
        }
        E.dirty_bottom = 0;
    }

    /* Position cursor */
    {
        int screen_y = E.cy - E.row_offset + 1;
        int screen_x = E.gutter_width + (E.cx - E.col_offset) + 1;
        term_gotoxy(screen_y, screen_x);
    }

    term_end_update();
}

/* ---- Cursor Movement ---- */

static void editor_clamp_cx(void) {
    if (E.cy < 0) E.cy = 0;
    if (E.cy >= E.num_lines) E.cy = E.num_lines - 1;
    {
        int line_len = E.lines[E.cy].len;
        if (E.cx > line_len) E.cx = line_len;
    }
}

static int is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static void editor_move_word_left(void) {
    /* Skip non-word chars, then skip word chars */
    if (E.cx == 0 && E.cy > 0) {
        E.cy--;
        E.cx = E.lines[E.cy].len;
    }
    if (E.cy < E.num_lines) {
        line_t *l = &E.lines[E.cy];
        while (E.cx > 0 && !is_word_char(l->text[E.cx - 1]))
            E.cx--;
        while (E.cx > 0 && is_word_char(l->text[E.cx - 1]))
            E.cx--;
    }
}

static void editor_move_word_right(void) {
    if (E.cy < E.num_lines) {
        line_t *l = &E.lines[E.cy];
        /* Skip word chars, then skip non-word chars */
        while (E.cx < l->len && is_word_char(l->text[E.cx]))
            E.cx++;
        while (E.cx < l->len && !is_word_char(l->text[E.cx]))
            E.cx++;
        if (E.cx >= l->len && E.cy < E.num_lines - 1) {
            E.cy++;
            E.cx = 0;
        }
    }
}

static void editor_move_cursor(int key) {
    switch (key) {
    case KEY_UP:
        if (E.cy > 0) E.cy--;
        editor_clamp_cx();
        break;
    case KEY_DOWN:
        if (E.cy < E.num_lines - 1) E.cy++;
        editor_clamp_cx();
        break;
    case KEY_LEFT:
        if (E.cx > 0) {
            E.cx--;
        } else if (E.cy > 0) {
            E.cy--;
            E.cx = E.lines[E.cy].len;
        }
        break;
    case KEY_RIGHT:
        if (E.cy < E.num_lines) {
            int line_len = E.lines[E.cy].len;
            if (E.cx < line_len) {
                E.cx++;
            } else if (E.cy < E.num_lines - 1) {
                E.cy++;
                E.cx = 0;
            }
        }
        break;
    case KEY_HOME:
    case CTRL('a'):
        E.cx = 0;
        break;
    case KEY_END:
    case CTRL('e'):
        if (E.cy < E.num_lines)
            E.cx = E.lines[E.cy].len;
        break;
    case KEY_PGUP:
        if (E.cy > E.edit_rows)
            E.cy -= E.edit_rows;
        else
            E.cy = 0;
        editor_clamp_cx();
        break;
    case KEY_PGDN:
        E.cy += E.edit_rows;
        if (E.cy >= E.num_lines) E.cy = E.num_lines - 1;
        editor_clamp_cx();
        break;
    case KEY_CTRL_LEFT:
        editor_move_word_left();
        break;
    case KEY_CTRL_RIGHT:
        editor_move_word_right();
        break;
    }
}

/* ---- Editing Operations ---- */

static void editor_insert_char(int ch) {
    if (E.cy >= E.num_lines) return;
    line_insert_char(&E.lines[E.cy], E.cx, (char)ch);
    E.cx++;
    E.modified = 1;
    E.quit_count = 0;
    dirty_file_row(E.cy);
}

static void editor_insert_newline(void) {
    if (E.cy >= E.num_lines) return;

    /* Capture indent from current line before splitting */
    int indent = 0;
    {
        line_t *l = &E.lines[E.cy];
        while (indent < l->len && l->text[indent] == ' ')
            indent++;
    }

    editor_split_line(E.cy, E.cx);
    E.cy++;
    E.cx = 0;

    /* Auto-indent: prepend leading spaces from previous line */
    if (indent > 0) {
        line_t *newl = &E.lines[E.cy];
        int old_len = newl->len;
        line_ensure_cap(newl, old_len + indent + 1);
        memmove(newl->text + indent, newl->text, old_len + 1);
        memset(newl->text, ' ', indent);
        newl->len += indent;
        E.cx = indent;
    }

    E.modified = 1;
    E.quit_count = 0;
    dirty_from_file_row(E.cy - 1);
    E.dirty_status = 1;
}

static void editor_insert_tab(void) {
    int spaces = TAB_STOP - (E.cx % TAB_STOP);
    int i;
    for (i = 0; i < spaces; i++)
        editor_insert_char(' ');
}

static void editor_backspace(void) {
    if (E.cy >= E.num_lines) return;
    if (E.cx > 0) {
        line_delete_char(&E.lines[E.cy], E.cx - 1);
        E.cx--;
        E.modified = 1;
        dirty_file_row(E.cy);
    } else if (E.cy > 0) {
        /* Join with previous line */
        E.cx = E.lines[E.cy - 1].len;
        editor_join_lines(E.cy - 1);
        E.cy--;
        E.modified = 1;
        dirty_from_file_row(E.cy);
        E.dirty_status = 1;
    }
    E.quit_count = 0;
}

static void editor_delete(void) {
    if (E.cy >= E.num_lines) return;
    if (E.cx < E.lines[E.cy].len) {
        line_delete_char(&E.lines[E.cy], E.cx);
        E.modified = 1;
        dirty_file_row(E.cy);
    } else if (E.cy < E.num_lines - 1) {
        /* Join with next line */
        editor_join_lines(E.cy);
        E.modified = 1;
        dirty_from_file_row(E.cy);
        E.dirty_status = 1;
    }
    E.quit_count = 0;
}

static void editor_cut_line(void) {
    if (E.cy >= E.num_lines) return;
    /* Store in cut buffer */
    if (E.has_cut)
        line_free(&E.cut_line);
    line_init(&E.cut_line);
    line_set(&E.cut_line, E.lines[E.cy].text, E.lines[E.cy].len);
    E.has_cut = 1;

    editor_delete_line(E.cy);
    if (E.num_lines == 0)
        editor_insert_line(0, "", 0);
    if (E.cy >= E.num_lines)
        E.cy = E.num_lines - 1;
    editor_clamp_cx();
    E.modified = 1;
    E.quit_count = 0;
    editor_set_message("Line cut");
    dirty_from_file_row(E.cy);
    E.dirty_status = 1;
}

static void editor_duplicate_line(void) {
    if (E.cy >= E.num_lines) return;
    line_t *l = &E.lines[E.cy];
    editor_insert_line(E.cy + 1, l->text, l->len);
    E.cy++;
    E.modified = 1;
    E.quit_count = 0;
    dirty_from_file_row(E.cy - 1);
    E.dirty_status = 1;
}

static void editor_paste_line(void) {
    if (!E.has_cut) {
        editor_set_message("Nothing to paste");
        return;
    }
    editor_insert_line(E.cy + 1, E.cut_line.text, E.cut_line.len);
    E.cy++;
    E.cx = 0;
    E.modified = 1;
    E.quit_count = 0;
    editor_set_message("Line pasted");
    dirty_from_file_row(E.cy);
    E.dirty_status = 1;
}

/* ---- Search ---- */

static void editor_prompt(const char *prompt, char *buf, int bufsize) {
    int pos = strlen(buf);
    int key;

    for (;;) {
        /* Draw prompt on bottom line */
        term_gotoxy(E.screen_rows, 1);
        term_set_attr(7);
        {
            int len, i;
            term_puts(prompt);
            len = strlen(prompt);
            /* Show current input */
            for (i = 0; i < pos && len + i < E.screen_cols - 1; i++)
                term_putc(buf[i]);
            len += i;
            for (; len < E.screen_cols; len++)
                term_putc(' ');
        }
        term_set_attr(0);
        /* Position cursor in prompt */
        term_gotoxy(E.screen_rows, (int)strlen(prompt) + pos + 1);

        key = read_key();
        if (key == '\r' || key == '\n') {
            break;
        }
        if (key == 27 || key == -1) {
            buf[0] = '\0';
            break;
        }
        if (key == 8) { /* backspace */
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
            }
            continue;
        }
        if (key >= 32 && key < 127 && pos < bufsize - 1) {
            buf[pos++] = (char)key;
            buf[pos] = '\0';
        }
    }
    E.message[0] = '\0'; /* clear message area */
}

static void editor_find(void) {
    int orig_cy = E.cy;
    int orig_cx = E.cx;

    editor_prompt("Search: ", E.search_buf, MAX_SEARCH);
    if (E.search_buf[0] == '\0') {
        editor_set_message("Search cancelled");
        return;
    }

    /* Search forward from current position */
    {
        int slen = strlen(E.search_buf);
        int y, start_x;

        /* Start from current line, position after cursor */
        for (y = E.cy; y < E.num_lines; y++) {
            line_t *l = &E.lines[y];
            start_x = (y == E.cy) ? E.cx + 1 : 0;
            {
                int x;
                for (x = start_x; x <= l->len - slen; x++) {
                    if (memcmp(l->text + x, E.search_buf, slen) == 0) {
                        E.cy = y;
                        E.cx = x;
                        editor_set_message("Found");
                        return;
                    }
                }
            }
        }
        /* Wrap to beginning */
        for (y = 0; y <= orig_cy; y++) {
            line_t *l = &E.lines[y];
            int limit = (y == orig_cy) ? orig_cx : l->len - slen;
            {
                int x;
                for (x = 0; x <= limit; x++) {
                    if (x <= l->len - slen &&
                        memcmp(l->text + x, E.search_buf, slen) == 0) {
                        E.cy = y;
                        E.cx = x;
                        editor_set_message("Found (wrapped)");
                        return;
                    }
                }
            }
        }
    }
    editor_set_message("Not found");
}

static void editor_find_replace(void) {
    char replace_buf[MAX_SEARCH] = "";
    int slen, rlen;
    int replaced = 0;

    editor_prompt("Search: ", E.search_buf, MAX_SEARCH);
    if (E.search_buf[0] == '\0') {
        editor_set_message("Replace cancelled");
        return;
    }
    editor_prompt("Replace with: ", replace_buf, MAX_SEARCH);
    /* Empty replacement is valid (delete occurrences) */

    slen = strlen(E.search_buf);
    rlen = strlen(replace_buf);

    /* Search forward from current position, prompt for each */
    {
        int y, x, start_x;
        for (y = E.cy; y < E.num_lines; y++) {
            line_t *l = &E.lines[y];
            start_x = (y == E.cy) ? E.cx : 0;
            for (x = start_x; x <= l->len - slen; x++) {
                if (memcmp(l->text + x, E.search_buf, slen) == 0) {
                    char confirm[8] = "";
                    /* Move cursor to match */
                    E.cy = y;
                    E.cx = x;
                    dirty_file_row(y);
                    editor_refresh_screen();

                    editor_prompt("Replace? (y/n/a/q): ", confirm, sizeof(confirm));
                    if (confirm[0] == 'q' || confirm[0] == '\0')
                        goto done;

                    if (confirm[0] == 'y' || confirm[0] == 'a') {
                        /* Delete old, insert new */
                        int tail = l->len - x - slen;
                        line_ensure_cap(l, l->len - slen + rlen + 1);
                        /* Re-get pointer after potential realloc */
                        l = &E.lines[y];
                        memmove(l->text + x + rlen, l->text + x + slen, tail + 1);
                        memcpy(l->text + x, replace_buf, rlen);
                        l->len += rlen - slen;
                        E.cx = x + rlen;
                        E.modified = 1;
                        replaced++;
                        dirty_file_row(y);
                        x += rlen - 1; /* advance past replacement */

                        if (confirm[0] == 'a') {
                            /* Replace all remaining without prompting */
                            for (x = x + 1; x <= l->len - slen; x++) {
                                if (memcmp(l->text + x, E.search_buf, slen) == 0) {
                                    int t2 = l->len - x - slen;
                                    line_ensure_cap(l, l->len - slen + rlen + 1);
                                    l = &E.lines[y];
                                    memmove(l->text + x + rlen, l->text + x + slen, t2 + 1);
                                    memcpy(l->text + x, replace_buf, rlen);
                                    l->len += rlen - slen;
                                    replaced++;
                                    x += rlen - 1;
                                }
                            }
                            dirty_file_row(y);
                            for (y = y + 1; y < E.num_lines; y++) {
                                l = &E.lines[y];
                                for (x = 0; x <= l->len - slen; x++) {
                                    if (memcmp(l->text + x, E.search_buf, slen) == 0) {
                                        int t2 = l->len - x - slen;
                                        line_ensure_cap(l, l->len - slen + rlen + 1);
                                        l = &E.lines[y];
                                        memmove(l->text + x + rlen, l->text + x + slen, t2 + 1);
                                        memcpy(l->text + x, replace_buf, rlen);
                                        l->len += rlen - slen;
                                        replaced++;
                                        x += rlen - 1;
                                    }
                                }
                                dirty_file_row(y);
                            }
                            goto done;
                        }
                    }
                }
            }
        }
    }
done:
    E.quit_count = 0;
    {
        char msg[MAX_MESSAGE];
        snprintf(msg, MAX_MESSAGE, "Replaced %d occurrence%s", replaced, replaced == 1 ? "" : "s");
        editor_set_message(msg);
    }
}

/* ---- Key Dispatch ---- */

static void editor_process_key(int key) {
    /* Clear message on any key (unless it's a message-producing action) */
    if (key != CTRL('q'))
        E.message[0] = '\0';

    switch (key) {
    case KEY_UP:
    case KEY_DOWN:
    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_HOME:
    case KEY_END:
    case KEY_PGUP:
    case KEY_PGDN:
    case CTRL('a'):
    case CTRL('e'):
    case KEY_CTRL_LEFT:
    case KEY_CTRL_RIGHT:
        editor_move_cursor(key);
        E.quit_count = 0;
        E.dirty_status = 1;  /* cursor position changed */
        break;

    case 8: /* backspace */
        editor_backspace();
        break;

    case KEY_DEL:
        editor_delete();
        break;

    case '\r':
    case '\n':
        editor_insert_newline();
        break;

    case '\t':
        editor_insert_tab();
        break;

    case CTRL('s'): /* save */
        editor_save();
        E.dirty_status = 1;
        break;

    case CTRL('q'): /* quit */
        if (E.modified) {
            E.quit_count++;
            if (E.quit_count < QUIT_CONFIRM) {
                snprintf(E.message, MAX_MESSAGE,
                         "Unsaved changes! Press Ctrl-Q again to quit without saving.");
                break;
            }
        }
        E.running = 0;
        break;

    case CTRL('x'): /* save and quit */
        if (editor_save() == 0)
            E.running = 0;
        E.dirty_status = 1;
        E.dirty_bottom = 1;
        break;

    case CTRL('f'): /* find */
        editor_find();
        E.dirty_status = 1;
        E.dirty_bottom = 1;
        break;

    case CTRL('k'): /* cut line */
        editor_cut_line();
        break;

    case CTRL('u'): /* paste line */
        editor_paste_line();
        break;

    case CTRL('d'): /* duplicate line */
        editor_duplicate_line();
        break;

    case CTRL('r'): /* find & replace */
        editor_find_replace();
        E.dirty_status = 1;
        E.dirty_bottom = 1;
        break;

    case CTRL('g'): /* go to line */
        {
            char linebuf[16] = "";
            int lineno;
            editor_prompt("Go to line: ", linebuf, sizeof(linebuf));
            if (linebuf[0] == '\0') break;
            lineno = atoi(linebuf);
            if (lineno < 1) lineno = 1;
            if (lineno > E.num_lines) lineno = E.num_lines;
            E.cy = lineno - 1;
            E.cx = 0;
            editor_clamp_cx();
            E.dirty_status = 1;
            E.dirty_bottom = 1;
        }
        break;

    case 27: /* escape - clear message */
        E.message[0] = '\0';
        E.quit_count = 0;
        break;

    default:
        /* Printable characters */
        if (key >= 32 && key < 127) {
            editor_insert_char(key);
        }
        break;
    }
}

/* ---- Buffer Unit Tests (--test flag) ---- */

static int test_count, test_pass;

static void test_assert(int cond, const char *msg) {
    test_count++;
    if (cond) {
        test_pass++;
    } else {
        printf("FAIL: %s\n", msg);
    }
}

static void editor_reset(void) {
    int i;
    for (i = 0; i < E.num_lines; i++)
        line_free(&E.lines[i]);
    E.num_lines = 0;
    E.cx = E.cy = 0;
    E.row_offset = E.col_offset = 0;
    E.modified = 0;
    E.message[0] = '\0';
}

static int run_tests(void) {
    test_count = 0;
    test_pass = 0;

    /* Initialize minimal editor state for tests */
    E.lines_cap = LINES_INIT_CAP;
    E.lines = malloc(E.lines_cap * sizeof(line_t));
    E.num_lines = 0;
    E.screen_rows = 24;
    E.screen_cols = 80;
    E.edit_rows = 22;
    E.has_cut = 0;

    printf("Running buffer unit tests...\n");

    /* Test 1: line_init and line_set */
    {
        line_t l;
        line_init(&l);
        test_assert(l.len == 0, "line_init: len == 0");
        test_assert(l.text[0] == '\0', "line_init: empty string");
        line_set(&l, "Hello", 5);
        test_assert(l.len == 5, "line_set: len == 5");
        test_assert(strcmp(l.text, "Hello") == 0, "line_set: text == Hello");
        line_free(&l);
    }

    /* Test 2: line_insert_char */
    {
        line_t l;
        line_init(&l);
        line_set(&l, "Hllo", 4);
        line_insert_char(&l, 1, 'e');
        test_assert(l.len == 5, "line_insert_char: len == 5");
        test_assert(strcmp(l.text, "Hello") == 0, "line_insert_char: text == Hello");
        line_free(&l);
    }

    /* Test 3: line_delete_char */
    {
        line_t l;
        line_init(&l);
        line_set(&l, "Heello", 6);
        line_delete_char(&l, 2);
        test_assert(l.len == 5, "line_delete_char: len == 5");
        test_assert(strcmp(l.text, "Hello") == 0, "line_delete_char: text == Hello");
        line_free(&l);
    }

    /* Test 4: editor_insert_line */
    {
        editor_reset();
        editor_insert_line(0, "First", 5);
        editor_insert_line(1, "Third", 5);
        editor_insert_line(1, "Second", 6);
        test_assert(E.num_lines == 3, "insert_line: 3 lines");
        test_assert(strcmp(E.lines[0].text, "First") == 0, "insert_line: line 0 == First");
        test_assert(strcmp(E.lines[1].text, "Second") == 0, "insert_line: line 1 == Second");
        test_assert(strcmp(E.lines[2].text, "Third") == 0, "insert_line: line 2 == Third");
    }

    /* Test 5: editor_delete_line */
    {
        editor_reset();
        editor_insert_line(0, "A", 1);
        editor_insert_line(1, "B", 1);
        editor_insert_line(2, "C", 1);
        editor_delete_line(1);
        test_assert(E.num_lines == 2, "delete_line: 2 lines");
        test_assert(strcmp(E.lines[0].text, "A") == 0, "delete_line: line 0 == A");
        test_assert(strcmp(E.lines[1].text, "C") == 0, "delete_line: line 1 == C");
    }

    /* Test 6: editor_split_line */
    {
        editor_reset();
        editor_insert_line(0, "HelloWorld", 10);
        editor_split_line(0, 5);
        test_assert(E.num_lines == 2, "split_line: 2 lines");
        test_assert(strcmp(E.lines[0].text, "Hello") == 0, "split_line: line 0 == Hello");
        test_assert(strcmp(E.lines[1].text, "World") == 0, "split_line: line 1 == World");
    }

    /* Test 7: editor_join_lines */
    {
        editor_reset();
        editor_insert_line(0, "Hello", 5);
        editor_insert_line(1, "World", 5);
        editor_join_lines(0);
        test_assert(E.num_lines == 1, "join_lines: 1 line");
        test_assert(strcmp(E.lines[0].text, "HelloWorld") == 0, "join_lines: text == HelloWorld");
    }

    /* Test 8: cut and paste */
    {
        editor_reset();
        editor_insert_line(0, "Line1", 5);
        editor_insert_line(1, "Line2", 5);
        editor_insert_line(2, "Line3", 5);
        E.cy = 1; E.cx = 0;
        /* Simulate cut */
        if (E.has_cut) line_free(&E.cut_line);
        line_init(&E.cut_line);
        line_set(&E.cut_line, E.lines[E.cy].text, E.lines[E.cy].len);
        E.has_cut = 1;
        editor_delete_line(E.cy);
        test_assert(E.num_lines == 2, "cut: 2 lines remain");
        test_assert(strcmp(E.cut_line.text, "Line2") == 0, "cut: buffer == Line2");
        /* Simulate paste */
        editor_insert_line(E.cy + 1, E.cut_line.text, E.cut_line.len);
        E.cy++;
        test_assert(E.num_lines == 3, "paste: 3 lines");
        test_assert(strcmp(E.lines[2].text, "Line2") == 0, "paste: line 2 == Line2");
    }

    /* Test 9: insert char at cursor */
    {
        editor_reset();
        editor_insert_line(0, "AC", 2);
        E.cy = 0; E.cx = 1;
        line_insert_char(&E.lines[0], E.cx, 'B');
        E.cx++;
        test_assert(strcmp(E.lines[0].text, "ABC") == 0, "insert_char: text == ABC");
        test_assert(E.cx == 2, "insert_char: cx == 2");
    }

    /* Test 10: backspace joining lines */
    {
        editor_reset();
        editor_insert_line(0, "Hello", 5);
        editor_insert_line(1, "World", 5);
        E.cy = 1; E.cx = 0;
        /* Backspace at start of line should join with previous */
        E.cx = E.lines[E.cy - 1].len;
        editor_join_lines(E.cy - 1);
        E.cy--;
        test_assert(E.num_lines == 1, "backspace-join: 1 line");
        test_assert(strcmp(E.lines[0].text, "HelloWorld") == 0, "backspace-join: text == HelloWorld");
        test_assert(E.cx == 5, "backspace-join: cx == 5");
    }

    /* Test 11: gutter width calculation */
    {
        editor_reset();
        int i;
        for (i = 0; i < 99; i++)
            editor_insert_line(i, "", 0);
        test_assert(compute_gutter_width() == 3, "gutter_width: 99 lines -> 3");
        editor_insert_line(99, "", 0);
        test_assert(compute_gutter_width() == 4, "gutter_width: 100 lines -> 4");
    }

    /* Test 12: line_append */
    {
        line_t l;
        line_init(&l);
        line_set(&l, "Hello", 5);
        line_append(&l, " World", 6);
        test_assert(l.len == 11, "line_append: len == 11");
        test_assert(strcmp(l.text, "Hello World") == 0, "line_append: text == Hello World");
        line_free(&l);
    }

    /* Test 13: auto-indent (newline preserves leading spaces) */
    {
        editor_reset();
        editor_insert_line(0, "    indented text", 17);
        E.cy = 0; E.cx = 17; /* end of line */
        /* Simulate what editor_insert_newline does */
        {
            int indent = 0;
            line_t *l = &E.lines[E.cy];
            while (indent < l->len && l->text[indent] == ' ')
                indent++;
            editor_split_line(E.cy, E.cx);
            E.cy++;
            E.cx = 0;
            if (indent > 0) {
                line_t *newl = &E.lines[E.cy];
                int old_len = newl->len;
                line_ensure_cap(newl, old_len + indent + 1);
                memmove(newl->text + indent, newl->text, old_len + 1);
                memset(newl->text, ' ', indent);
                newl->len += indent;
                E.cx = indent;
            }
        }
        test_assert(E.num_lines == 2, "auto-indent: 2 lines");
        test_assert(E.cx == 4, "auto-indent: cx == 4");
        test_assert(memcmp(E.lines[1].text, "    ", 4) == 0, "auto-indent: 4 leading spaces");
    }

    /* Test 14: duplicate line */
    {
        editor_reset();
        editor_insert_line(0, "Hello", 5);
        editor_insert_line(1, "World", 5);
        E.cy = 0; E.cx = 0;
        /* Simulate editor_duplicate_line */
        editor_insert_line(E.cy + 1, E.lines[E.cy].text, E.lines[E.cy].len);
        E.cy++;
        test_assert(E.num_lines == 3, "dup_line: 3 lines");
        test_assert(strcmp(E.lines[0].text, "Hello") == 0, "dup_line: line 0 == Hello");
        test_assert(strcmp(E.lines[1].text, "Hello") == 0, "dup_line: line 1 == Hello (dup)");
        test_assert(strcmp(E.lines[2].text, "World") == 0, "dup_line: line 2 == World");
    }

    /* Test 15: word movement */
    {
        editor_reset();
        editor_insert_line(0, "hello world foo_bar", 19);
        E.cy = 0; E.cx = 0;
        /* Move word right: should skip "hello" then space -> land on "world" */
        {
            line_t *l = &E.lines[E.cy];
            while (E.cx < l->len && is_word_char(l->text[E.cx])) E.cx++;
            while (E.cx < l->len && !is_word_char(l->text[E.cx])) E.cx++;
        }
        test_assert(E.cx == 6, "word_right: cx == 6 (start of world)");
        /* Move word left from position 6: skip back over space, then "hello" */
        {
            line_t *l = &E.lines[E.cy];
            while (E.cx > 0 && !is_word_char(l->text[E.cx - 1])) E.cx--;
            while (E.cx > 0 && is_word_char(l->text[E.cx - 1])) E.cx--;
        }
        test_assert(E.cx == 0, "word_left: cx == 0 (start of hello)");
    }

    /* Test 16: find & replace (line-level string replacement) */
    {
        editor_reset();
        editor_insert_line(0, "foo bar foo baz", 15);
        /* Manually replace first "foo" with "qux" */
        {
            line_t *l = &E.lines[0];
            char *search = "foo";
            char *replace = "qux";
            int slen = 3, rlen = 3;
            int x;
            for (x = 0; x <= l->len - slen; x++) {
                if (memcmp(l->text + x, search, slen) == 0) {
                    memmove(l->text + x + rlen, l->text + x + slen, l->len - x - slen + 1);
                    memcpy(l->text + x, replace, rlen);
                    l->len += rlen - slen;
                    x += rlen - 1;
                }
            }
        }
        test_assert(strcmp(E.lines[0].text, "qux bar qux baz") == 0,
                    "replace: foo->qux in 'foo bar foo baz'");
    }

    /* Cleanup */
    editor_reset();
    if (E.has_cut) {
        line_free(&E.cut_line);
        E.has_cut = 0;
    }
    free(E.lines);

    printf("%d/%d tests passed\n", test_pass, test_count);
    return (test_pass == test_count) ? 0 : 1;
}

/* ---- Main ---- */

int main(int argc, char *argv[]) {
    int i;

    /* Check for --test flag */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0)
            return run_tests();
    }

    /* Initialize terminal */
    if (term_init() != 0) {
        printf("Error: terminal service not available\n");
        return 1;
    }

    /* Initialize editor state */
    memset(&E, 0, sizeof(E));
    E.lines_cap = LINES_INIT_CAP;
    E.lines = malloc(E.lines_cap * sizeof(line_t));
    E.num_lines = 0;
    E.running = 1;

    term_get_size(&E.screen_rows, &E.screen_cols);
    if (E.screen_rows < 5) E.screen_rows = 24;
    if (E.screen_cols < 20) E.screen_cols = 80;
    E.edit_rows = E.screen_rows - 2; /* status bar + help/message bar */

    /* Load file or start empty */
    if (argc >= 2 && argv[1][0] != '-') {
        editor_load(argv[1]);
    } else {
        editor_insert_line(0, "", 0);
        editor_set_message("nano for SLOW-32  ^S=Save ^Q=Quit ^X=Save+Quit ^F=Find");
    }

    E.gutter_width = compute_gutter_width();
    E.prev_gutter_width = E.gutter_width;
    E.prev_num_lines = E.num_lines;
    E.prev_row_offset = -1;  /* force initial dirty_all */
    E.prev_col_offset = -1;

    /* Enter raw mode and clear screen */
    term_set_raw(1);
    term_clear(0);

    /* Main loop */
    while (E.running) {
        int key;
        editor_refresh_screen();
        key = read_key();
        if (key == -1) break; /* EOF */
        editor_process_key(key);
    }

    /* Cleanup */
    term_set_attr(0);
    term_set_raw(0);
    term_clear(0);
    term_gotoxy(1, 1);
    term_cleanup();

    /* Free memory */
    for (i = 0; i < E.num_lines; i++)
        line_free(&E.lines[i]);
    free(E.lines);
    if (E.has_cut)
        line_free(&E.cut_line);

    return 0;
}
