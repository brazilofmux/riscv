#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "label.h"
#include "util.h"

void lbl_init(lbl_def_t *def) {
    memset(def, 0, sizeof(*def));
    def->height = 5;
    def->width = 35;
    def->left_margin = 0;
    def->lines_between = 1;
    def->spaces_between = 0;
    def->across = 1;
}

/* ---- Binary .LBL reader ---- */

static uint16_t read_le16(const unsigned char *p) {
    return p[0] | (p[1] << 8);
}

static void write_le16(unsigned char *p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

int lbl_read(const char *filename, lbl_def_t *def) {
    unsigned char buf[LBL_FILE_SIZE];
    FILE *fp;
    int i;

    fp = fopen(filename, "rb");
    if (!fp) return -1;

    memset(buf, 0, sizeof(buf));
    if (fread(buf, 1, LBL_FILE_SIZE, fp) < LBL_FILE_SIZE) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* Verify signature */
    if (buf[0] != 0x02) return -1;

    lbl_init(def);

    /* Remark: bytes 1-60 */
    memcpy(def->remark, buf + 1, 60);
    def->remark[60] = '\0';
    trim_right(def->remark);

    /* Parameters */
    def->height        = read_le16(buf + 61);
    def->width         = read_le16(buf + 63);
    def->left_margin   = read_le16(buf + 65);
    def->lines_between = read_le16(buf + 67);
    def->spaces_between= read_le16(buf + 69);
    def->across        = read_le16(buf + 71);

    if (def->height > LBL_MAX_LINES) def->height = LBL_MAX_LINES;

    /* Content lines: 16 x 60 bytes at offset 73 */
    def->num_lines = def->height;
    for (i = 0; i < LBL_MAX_LINES; i++) {
        memcpy(def->lines[i], buf + 73 + i * 60, 60);
        def->lines[i][60] = '\0';
        trim_right(def->lines[i]);
    }

    return 0;
}

int lbl_write(const char *filename, const lbl_def_t *def) {
    unsigned char buf[LBL_FILE_SIZE];
    FILE *fp;
    int i;

    memset(buf, ' ', sizeof(buf));  /* Space-pad by default */

    /* Signature */
    buf[0] = 0x02;

    /* Remark (space-padded) */
    {
        int len = strlen(def->remark);
        if (len > 60) len = 60;
        memcpy(buf + 1, def->remark, len);
    }

    /* Parameters */
    write_le16(buf + 61, def->height);
    write_le16(buf + 63, def->width);
    write_le16(buf + 65, def->left_margin);
    write_le16(buf + 67, def->lines_between);
    write_le16(buf + 69, def->spaces_between);
    write_le16(buf + 71, def->across);

    /* Content lines (space-padded to 60 chars each) */
    for (i = 0; i < LBL_MAX_LINES; i++) {
        int offset = 73 + i * 60;
        /* First clear to spaces (already done by memset) */
        if (i < def->num_lines && def->lines[i][0]) {
            int len = strlen(def->lines[i]);
            if (len > 60) len = 60;
            memcpy(buf + offset, def->lines[i], len);
        }
    }

    /* End marker */
    buf[1033] = 0x02;

    fp = fopen(filename, "wb");
    if (!fp) return -1;
    fwrite(buf, 1, LBL_FILE_SIZE, fp);
    fclose(fp);
    return 0;
}

/* ---- Label generation ---- */

void label_generate(lbl_def_t *def, dbf_t *db, expr_ctx_t *ectx,
                    const char *for_cond, int sample,
                    FILE *outfile) {
    /* row_buffer[across][height][width+1] */
    char row_buf[16][LBL_MAX_LINES][256];
    int col_count = 0;
    uint32_t rec;
    int line, label, j;

    if (sample) {
        /* Print one sample label with X placeholders */
        int margin;
        for (margin = 0; margin < def->left_margin; margin++)
            fputc(' ', outfile);

        for (line = 0; line < def->height; line++) {
            int k;
            for (margin = 0; margin < def->left_margin; margin++)
                fputc(' ', outfile);
            for (k = 0; k < def->width; k++)
                fputc('X', outfile);
            fprintf(outfile, "\n");
        }
        return;
    }

    for (rec = 1; rec <= db->record_count; rec++) {
        if (dbf_read_record(db, rec) < 0) continue;
        if (db->record_buf[0] == '*') continue;

        /* FOR condition */
        if (for_cond && for_cond[0]) {
            value_t cond;
            if (expr_eval_str(ectx, for_cond, &cond) != 0) continue;
            if (cond.type != VAL_LOGIC || !cond.logic) continue;
        }

        /* Evaluate all lines for this label */
        for (line = 0; line < def->height; line++) {
            row_buf[col_count][line][0] = '\0';
            if (line < def->num_lines && def->lines[line][0]) {
                value_t v;
                if (expr_eval_str(ectx, def->lines[line], &v) == 0) {
                    val_to_string(&v, row_buf[col_count][line], 256);
                    trim_right(row_buf[col_count][line]);
                }
            }
            /* Pad to label width */
            {
                int len = strlen(row_buf[col_count][line]);
                while (len < def->width) {
                    row_buf[col_count][line][len] = ' ';
                    len++;
                }
                row_buf[col_count][line][def->width] = '\0';
            }
        }

        col_count++;

        /* Flush row when we have enough labels across */
        if (col_count >= def->across) {
            for (line = 0; line < def->height; line++) {
                int margin;
                for (margin = 0; margin < def->left_margin; margin++)
                    fputc(' ', outfile);
                for (label = 0; label < col_count; label++) {
                    fprintf(outfile, "%s", row_buf[label][line]);
                    if (label < col_count - 1) {
                        for (j = 0; j < def->spaces_between; j++)
                            fputc(' ', outfile);
                    }
                }
                fprintf(outfile, "\n");
            }
            /* Lines between label rows */
            for (j = 0; j < def->lines_between; j++)
                fprintf(outfile, "\n");
            col_count = 0;
        }
    }

    /* Flush partial row */
    if (col_count > 0) {
        for (line = 0; line < def->height; line++) {
            int margin;
            for (margin = 0; margin < def->left_margin; margin++)
                fputc(' ', outfile);
            for (label = 0; label < col_count; label++) {
                fprintf(outfile, "%s", row_buf[label][line]);
                if (label < col_count - 1) {
                    for (j = 0; j < def->spaces_between; j++)
                        fputc(' ', outfile);
                }
            }
            fprintf(outfile, "\n");
        }
    }
}
