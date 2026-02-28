#ifndef LABEL_H
#define LABEL_H

#include <stdio.h>
#include "dbf.h"
#include "expr.h"

#define LBL_FILE_SIZE       1034
#define LBL_MAX_LINES       16
#define LBL_EXPR_SIZE       60

typedef struct {
    char remark[61];        /* Description */
    int height;             /* Lines per label */
    int width;              /* Characters per label */
    int left_margin;        /* Left margin */
    int lines_between;      /* Blank lines between label rows */
    int spaces_between;     /* Spaces between labels across */
    int across;             /* Labels per row (1-N) */
    int num_lines;          /* Actual content lines (= height) */
    char lines[LBL_MAX_LINES][LBL_EXPR_SIZE + 1];  /* Content expressions */
} lbl_def_t;

/* Initialize with defaults */
void lbl_init(lbl_def_t *def);

/* Read/write binary .LBL file (return 0=ok, -1=error) */
int lbl_read(const char *filename, lbl_def_t *def);
int lbl_write(const char *filename, const lbl_def_t *def);

/* Generate label output */
void label_generate(lbl_def_t *def, dbf_t *db, expr_ctx_t *ectx,
                    const char *for_cond, int sample,
                    FILE *outfile);

#endif
