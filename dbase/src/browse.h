#ifndef BROWSE_H
#define BROWSE_H

#include "dbf.h"

#define BROWSE_MAX_ROWS 50

typedef struct {
    dbf_t *db;
    int screen_rows, screen_cols;
    int data_rows;                     /* screen_rows - 3 (header/sep/status) */

    uint32_t visible_recnos[BROWSE_MAX_ROWS];
    int visible_count;

    int cur_row;                       /* 0-based in visible_recnos */
    int cur_field;                     /* 0-based in field list */

    int first_field;                   /* horizontal scroll offset */
    int col_x[DBF_MAX_FIELDS];        /* screen x per visible column */
    int col_w[DBF_MAX_FIELDS];        /* width per visible column */
    int num_visible_cols;

    int field_list[DBF_MAX_FIELDS];   /* field indices (-1 terminated) */
    int field_count;                   /* 0 = all fields */

    int is_edit;                       /* 1 = EDIT mode, 0 = BROWSE mode */
} browse_state_t;

/* BROWSE command: spreadsheet grid view */
void cmd_browse(dbf_t *db, const char *args);

/* EDIT/CHANGE command: single-record vertical view */
void cmd_edit(dbf_t *db, const char *args);

#endif
