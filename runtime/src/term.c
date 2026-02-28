/*
 * term.c — Terminal service implementation for RV32IM
 *
 * Uses ANSI escape sequences for all output and 3 ECALLs for host-only ops:
 *   500: term_setraw(mode) — raw/cooked terminal mode
 *   501: term_getsize(buf) — query terminal dimensions
 *   502: term_kbhit()      — non-blocking key check
 */

#include <term.h>
#include <string.h>

/* Host ECALL wrappers (defined in term_syscall.s) */
extern int _term_setraw(int mode);
extern int _term_getsize(unsigned int *buf);
extern int _term_kbhit_raw(void);

/* Low-level I/O (defined in syscall.s) */
extern int _read(int fd, void *buf, int len);
extern int _write(int fd, const void *buf, int len);

/* ---- Output buffering ---- */

#define TERM_BUF_SIZE  8192

static char term_buf[TERM_BUF_SIZE];
static int term_buf_len;
static int term_buffering;  /* 1 = inside begin_update/end_update */

static void term_flush(void) {
    if (term_buf_len > 0) {
        _write(1, term_buf, term_buf_len);
        term_buf_len = 0;
    }
}

static void term_emit(const char *s, int len) {
    if (term_buffering) {
        /* Accumulate into buffer */
        while (len > 0) {
            int space = TERM_BUF_SIZE - term_buf_len;
            if (space <= 0) {
                term_flush();
                space = TERM_BUF_SIZE;
            }
            int chunk = (len < space) ? len : space;
            memcpy(term_buf + term_buf_len, s, chunk);
            term_buf_len += chunk;
            s += chunk;
            len -= chunk;
        }
    } else {
        /* Write directly */
        _write(1, s, len);
    }
}

static void term_emit_str(const char *s) {
    term_emit(s, strlen(s));
}

/* ---- Integer to string helper (avoids sprintf in hot path) ---- */

static int int_to_str(int val, char *buf) {
    char tmp[12];
    int i = 0;
    int neg = 0;

    if (val < 0) {
        neg = 1;
        val = -val;
    }
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0) {
            tmp[i++] = '0' + (val % 10);
            val /= 10;
        }
    }

    int pos = 0;
    if (neg) buf[pos++] = '-';
    while (i > 0) buf[pos++] = tmp[--i];
    buf[pos] = '\0';
    return pos;
}

/* ---- Public API ---- */

int term_init(void) {
    term_buf_len = 0;
    term_buffering = 0;
    /* Probe terminal: if stdout is a pipe, _term_getsize fails and we
       return -1 so callers (e.g. dBASE) can use their fallback path. */
    unsigned int buf[2];
    if (_term_getsize(buf) != 0)
        return -1;
    return 0;
}

void term_cleanup(void) {
    term_flush();
}

void term_set_raw(int raw) {
    term_flush();
    _term_setraw(raw);
}

void term_get_size(int *rows, int *cols) {
    unsigned int buf[2];
    if (_term_getsize(buf) == 0) {
        *rows = (int)buf[0];
        *cols = (int)buf[1];
    } else {
        *rows = 24;
        *cols = 80;
    }
}

void term_gotoxy(int row, int col) {
    /* ESC [ row ; col H */
    char seq[24];
    int pos = 0;
    seq[pos++] = '\033';
    seq[pos++] = '[';
    pos += int_to_str(row, seq + pos);
    seq[pos++] = ';';
    pos += int_to_str(col, seq + pos);
    seq[pos++] = 'H';
    term_emit(seq, pos);
}

void term_clear(int mode) {
    switch (mode) {
    case 0: term_emit_str("\033[2J"); break;   /* full screen */
    case 1: term_emit_str("\033[K"); break;     /* to end of line */
    case 2: term_emit_str("\033[J"); break;     /* to end of screen */
    }
}

void term_set_attr(int attr) {
    switch (attr) {
    case 0: term_emit_str("\033[0m"); break;    /* normal */
    case 1: term_emit_str("\033[1m"); break;    /* bold */
    case 7: term_emit_str("\033[7m"); break;    /* reverse */
    default: term_emit_str("\033[0m"); break;
    }
}

void term_set_color(int fg, int bg) {
    char seq[16];
    int pos = 0;
    seq[pos++] = '\033';
    seq[pos++] = '[';
    pos += int_to_str(30 + fg, seq + pos);
    seq[pos++] = ';';
    pos += int_to_str(40 + bg, seq + pos);
    seq[pos++] = 'm';
    term_emit(seq, pos);
}

void term_putc(int ch) {
    char c = (char)ch;
    term_emit(&c, 1);
}

void term_puts(const char *s) {
    term_emit_str(s);
}

int term_getkey(void) {
    unsigned char ch;
    int n = _read(0, &ch, 1);
    if (n <= 0) return -1;
    return (int)ch;
}

int term_kbhit(void) {
    return _term_kbhit_raw();
}

int term_save_screen(void) {
    term_emit_str("\033[?1049h");
    if (!term_buffering) term_flush();
    return 0;
}

int term_restore_screen(void) {
    term_emit_str("\033[?1049l");
    if (!term_buffering) term_flush();
    return 0;
}

void term_begin_update(void) {
    term_buffering = 1;
    term_buf_len = 0;
}

void term_end_update(void) {
    term_flush();
    term_buffering = 0;
}
