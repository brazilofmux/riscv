#ifndef SBASIC_TERMINAL_H
#define SBASIC_TERMINAL_H

int sb_term_init(void);
void sb_term_shutdown(void);
void sb_term_cls(void);
void sb_term_locate(int row, int col);
void sb_term_color(int fg, int bg, int has_fg, int has_bg);
int sb_term_inkey(char out[2]);

#endif
