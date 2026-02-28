#ifndef _TERM_H
#define _TERM_H

/* Terminal service API (negotiated via service protocol) */

/* Initialize the terminal service. Returns 0 on success, -1 if denied/unavailable. */
int  term_init(void);

/* Release the terminal service and restore terminal state. */
void term_cleanup(void);

/* Set terminal mode: 1=raw, 0=cooked. */
void term_set_raw(int raw);

/* Get terminal dimensions. */
void term_get_size(int *rows, int *cols);

/* Move cursor to (row, col), 1-based. */
void term_gotoxy(int row, int col);

/* Clear: 0=full screen, 1=to end of line, 2=to end of screen. */
void term_clear(int mode);

/* Set text attribute: 0=normal, 1=bold, 7=reverse. */
void term_set_attr(int attr);

/* Blocking key read. Returns character or -1 on EOF. */
int  term_getkey(void);

/* Non-blocking key poll. Returns 1 if key available, 0 otherwise. */
int  term_kbhit(void);

/* Set foreground and background color (ANSI 0-7). */
void term_set_color(int fg, int bg);

/* Output a single character at the cursor position. */
void term_putc(int ch);

/* Output a string at the cursor position. */
void term_puts(const char *s);

/* Save screen contents (push onto emulator-side stack). Returns 0 on success. */
int term_save_screen(void);

/* Restore screen contents (pop from emulator-side stack). Returns 0 on success. */
int term_restore_screen(void);

/* Begin buffered update: subsequent term ops update shadow buffer only (no stdout).
   Call term_end_update() to diff and emit minimum ANSI. */
void term_begin_update(void);

/* End buffered update: diff shadow buffer vs snapshot, emit only changed cells. */
void term_end_update(void);

#endif /* _TERM_H */
