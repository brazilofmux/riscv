/* terminal.c — ANSI-only terminal support for RV32IM port
 * No MMIO term service; straight ANSI escape codes.
 */
#include "terminal.h"
#include <stdio.h>

static int cur_fg = 7;
static int cur_bg = 0;

int sb_term_init(void) {
    return 0;
}

void sb_term_shutdown(void) {
}

void sb_term_cls(void) {
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
}

void sb_term_locate(int row, int col) {
    if (row < 1) row = 1;
    if (col < 1) col = 1;
    printf("\033[%d;%dH", row, col);
    fflush(stdout);
}

void sb_term_color(int fg, int bg, int has_fg, int has_bg) {
    if (!has_fg && !has_bg) {
        cur_fg = 7;
        cur_bg = 0;
        fputs("\033[0m", stdout);
        fflush(stdout);
        return;
    }

    if (has_fg) {
        cur_fg = fg;
        if (cur_fg < 0) cur_fg = 0;
        if (cur_fg > 15) cur_fg = 15;
    }
    if (has_bg) {
        cur_bg = bg;
        if (cur_bg < 0) cur_bg = 0;
        if (cur_bg > 15) cur_bg = 15;
    }

    if (cur_fg < 8)
        printf("\033[%d", 30 + cur_fg);
    else
        printf("\033[%d", 90 + (cur_fg - 8));

    if (cur_bg < 8)
        printf(";%dm", 40 + cur_bg);
    else
        printf(";%dm", 100 + (cur_bg - 8));

    fflush(stdout);
}

int sb_term_inkey(char out[2]) {
    (void)out;
    return 0;
}
