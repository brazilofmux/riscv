#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "browse.h"
#include "command.h"
#include "program.h"
#include "screen.h"
#include "field.h"
#include "util.h"
#include "index.h"
#include "area.h"

#ifdef __slow32__
#include <term.h>
#define HAS_TERM 1
#else
#define HAS_TERM 0
#endif

/* dBase III key codes (must match screen.c definitions) */
#define DBASE_KEY_UP     5
#define DBASE_KEY_DOWN  24
#define DBASE_KEY_LEFT  19
#define DBASE_KEY_RIGHT  4
#define DBASE_KEY_PGUP  18
#define DBASE_KEY_PGDN   3
#define DBASE_KEY_HOME   1
#define DBASE_KEY_END    6
#define DBASE_KEY_INS   22
#define DBASE_KEY_DEL    7
#define DBASE_KEY_SHIFT_TAB 256

/* ------------------------------------------------------------------ */
/*  Shared helpers                                                     */
/* ------------------------------------------------------------------ */

static int total_fields(browse_state_t *bs)
{
    return bs->field_count ? bs->field_count : bs->db->field_count;
}

static int get_field_index(browse_state_t *bs, int i)
{
    return bs->field_count ? bs->field_list[i] : i;
}

/* Navigate to next valid (non-deleted, non-filtered) record.
   Returns 0 on success, -1 if no more records. */
static int nav_next(browse_state_t *bs)
{
    dbf_t *db = bs->db;
    index_t *idx = cmd_controlling_index();
    int i;
    for (i = 0; i < (int)db->record_count + 1; i++) {
        int rc;
        if (idx) {
            rc = index_next(idx);
            if (rc != 0) return -1;
            uint32_t r = index_current_recno(idx);
            if (r == 0) return -1;
            dbf_read_record(db, r);
        } else {
            if (db->current_record >= db->record_count) return -1;
            dbf_read_record(db, db->current_record + 1);
        }
        if (!cmd_skip_deleted(db->record_buf) && cmd_check_filter(db))
            return 0;
    }
    return -1;
}

/* Navigate to previous valid record. */
static int nav_prev(browse_state_t *bs)
{
    dbf_t *db = bs->db;
    index_t *idx = cmd_controlling_index();
    int i;
    for (i = 0; i < (int)db->record_count + 1; i++) {
        int rc;
        if (idx) {
            rc = index_prev(idx);
            if (rc != 0) return -1;
            uint32_t r = index_current_recno(idx);
            if (r == 0) return -1;
            dbf_read_record(db, r);
        } else {
            if (db->current_record <= 1) return -1;
            dbf_read_record(db, db->current_record - 1);
        }
        if (!cmd_skip_deleted(db->record_buf) && cmd_check_filter(db))
            return 0;
    }
    return -1;
}

/* Go to first valid record */
static int nav_top(browse_state_t *bs)
{
    dbf_t *db = bs->db;
    index_t *idx = cmd_controlling_index();
    if (idx) {
        index_top(idx);
        uint32_t r = index_current_recno(idx);
        if (r == 0) return -1;
        dbf_read_record(db, r);
    } else {
        if (db->record_count == 0) return -1;
        dbf_read_record(db, 1);
    }
    if (!cmd_skip_deleted(db->record_buf) && cmd_check_filter(db))
        return 0;
    return nav_next(bs);
}

/* Go to last valid record */
static int nav_bottom(browse_state_t *bs)
{
    dbf_t *db = bs->db;
    index_t *idx = cmd_controlling_index();
    if (idx) {
        index_bottom(idx);
        uint32_t r = index_current_recno(idx);
        if (r == 0) return -1;
        dbf_read_record(db, r);
    } else {
        if (db->record_count == 0) return -1;
        dbf_read_record(db, db->record_count);
    }
    if (!cmd_skip_deleted(db->record_buf) && cmd_check_filter(db))
        return 0;
    return nav_prev(bs);
}

/* Get display value for a field into buf (max bufsize) */
static void get_field_display(dbf_t *db, int fi, char *buf, int bufsize)
{
    char raw[256];
    dbf_get_field_raw(db, fi, raw, sizeof(raw));
    switch (db->fields[fi].type) {
    case 'C': field_display_char(buf, raw, db->fields[fi].length); break;
    case 'F':
    case 'N': field_display_numeric(buf, raw, db->fields[fi].length); break;
    case 'D': field_display_date(buf, raw); break;
    case 'L': field_display_logical(buf, raw); break;
    case 'M': str_copy(buf, "memo", bufsize); break;
    default:  str_copy(buf, raw, bufsize); break;
    }
}

/* ------------------------------------------------------------------ */
/*  Cell editing (shared by EDIT and BROWSE)                           */
/* ------------------------------------------------------------------ */

#if HAS_TERM
/* Edit a field value in-place on screen.
   screen_row/screen_col: 1-based position on screen.
   width: display width of field.
   trigger_key: 0=Enter (cursor at start), >0=character that started edit.
   Returns 1 if accepted, 0 if cancelled. */
static int edit_cell(browse_state_t *bs, int field_idx,
                     int screen_row, int screen_col, int width, int trigger_key)
{
    dbf_t *db = bs->db;
    char buf[256];
    char original[256];
    int pos, len;
    char type = db->fields[field_idx].type;
    int flen = db->fields[field_idx].length;

    if (type == 'M') return 0; /* memo not editable */

    /* Get current value */
    get_field_display(db, field_idx, buf, sizeof(buf));
    trim_right(buf);
    str_copy(original, buf, sizeof(original));

    if (width > flen) width = flen;
    if (width > 250) width = 250;

    if (trigger_key > 0 && trigger_key >= 32 && trigger_key < 127) {
        /* Start with the trigger character */
        buf[0] = (char)trigger_key;
        buf[1] = '\0';
        pos = 1;
    } else {
        pos = strlen(buf);
    }

    for (;;) {
        int key;
        /* Draw field value in reverse video */
        len = strlen(buf);
        term_begin_update();
        term_gotoxy(screen_row, screen_col);
        term_set_attr(7);
        {
            int i;
            for (i = 0; i < width; i++) {
                if (i < len) term_putc(buf[i]);
                else term_putc(' ');
            }
        }
        /* Position cursor */
        term_gotoxy(screen_row, screen_col + pos);
        term_end_update();

        key = read_dbase_key();
        if (key < 0) break;

        if (key == 27) { /* Escape - cancel */
            str_copy(buf, original, sizeof(buf));
            /* Redraw original */
            len = strlen(buf);
            term_gotoxy(screen_row, screen_col);
            term_set_attr(0);
            {
                int i;
                for (i = 0; i < width; i++) {
                    if (i < len) term_putc(buf[i]);
                    else term_putc(' ');
                }
            }
    
            return 0;
        }

        if (key == 13 || key == 10 || key == 9 ||
            key == DBASE_KEY_UP || key == DBASE_KEY_DOWN) {
            /* Accept — restore normal attribute before returning */
            term_set_attr(0);
            break;
        }

        if (key == 8) { /* Backspace */
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, strlen(buf) - pos + 1);
                pos--;
            }
            continue;
        }

        if (key == DBASE_KEY_LEFT) {
            if (pos > 0) pos--;
            continue;
        }
        if (key == DBASE_KEY_RIGHT) {
            if (pos < (int)strlen(buf)) pos++;
            continue;
        }
        if (key == DBASE_KEY_HOME) { pos = 0; continue; }
        if (key == DBASE_KEY_END)  { pos = strlen(buf); continue; }
        if (key == DBASE_KEY_DEL) {
            len = strlen(buf);
            if (pos < len) {
                memmove(buf + pos, buf + pos + 1, len - pos);
            }
            continue;
        }

        /* Type-specific input validation */
        if (key >= 32 && key < 127) {
            if (type == 'L') {
                /* Toggle T/F */
                if (key == 'T' || key == 't' || key == 'Y' || key == 'y')
                    str_copy(buf, "T", sizeof(buf));
                else if (key == 'F' || key == 'f' || key == 'N' || key == 'n')
                    str_copy(buf, "F", sizeof(buf));
                pos = 1;
                continue;
            }
            if (type == 'N') {
                if (key != '-' && key != '.' && key != ' ' &&
                    (key < '0' || key > '9')) continue;
            }
            /* Insert character if room */
            len = strlen(buf);
            if (len < flen) {
                memmove(buf + pos + 1, buf + pos, len - pos + 1);
                buf[pos] = (char)key;
                pos++;
            } else if (pos < len) {
                /* Overwrite mode at capacity */
                buf[pos] = (char)key;
                if (pos < len) pos++;
            }
        }
    }

    /* Accept: format and store */
    {
        char formatted[256];
        int ok = 0;
        char old_keys[MAX_INDEXES][MAX_INDEX_KEY + 1];

        switch (type) {
        case 'C':
            ok = (field_format_char(formatted, flen, buf) == 0);
            break;
        case 'F':
        case 'N':
            ok = (field_format_numeric(formatted, flen,
                    db->fields[field_idx].decimals, buf) == 0);
            break;
        case 'D':
            ok = (field_format_date(formatted, buf) == 0);
            break;
        case 'L':
            ok = (field_format_logical(formatted, buf) == 0);
            break;
        default:
            ok = 0;
            break;
        }

        if (!ok) return 0;

        cmd_indexes_capture_keys(db, old_keys);
        dbf_set_field_raw(db, field_idx, formatted);
        if (cmd_indexes_update_current(db, old_keys) < 0) {
            /* Revert on uniqueness violation */
            char old_fmt[256];
            switch (type) {
            case 'C': field_format_char(old_fmt, flen, original); break;
            case 'F':
            case 'N': field_format_numeric(old_fmt, flen,
                        db->fields[field_idx].decimals, original); break;
            case 'D': field_format_date(old_fmt, original); break;
            case 'L': field_format_logical(old_fmt, original); break;
            default: old_fmt[0] = '\0'; break;
            }
            dbf_set_field_raw(db, field_idx, old_fmt);
            return 0;
        }
        dbf_flush_record(db);
    }
    return 1;
}
#endif /* HAS_TERM */

/* ------------------------------------------------------------------ */
/*  EDIT (single-record vertical view)                                 */
/* ------------------------------------------------------------------ */

#if HAS_TERM
static void edit_paint_header(browse_state_t *bs)
{
    dbf_t *db = bs->db;
    int is_del = (db->record_buf[0] == '*');
    term_gotoxy(1, 1);
    term_set_attr(7);
    {
        char hdr[128];
        int i, len;
        snprintf(hdr, sizeof(hdr), " Record: %u/%u%s",
                 db->current_record, db->record_count,
                 is_del ? "  *Del*" : "");
        len = strlen(hdr);
        term_puts(hdr);
        for (i = len; i < bs->screen_cols; i++) term_putc(' ');
    }
    term_set_attr(0);
}

static void edit_paint_help(browse_state_t *bs)
{
    term_gotoxy(bs->screen_rows, 1);
    term_set_attr(7);
    {
        const char *help = " ^W=Save  Esc=Exit  PgUp/PgDn=Record  ^U=Del";
        int i, len = strlen(help);
        term_puts(help);
        for (i = len; i < bs->screen_cols; i++) term_putc(' ');
    }
    term_set_attr(0);
}

static void edit_paint_record(browse_state_t *bs)
{
    dbf_t *db = bs->db;
    int nf = total_fields(bs);
    int max_name_w = 0;
    int r, fi, i;

    /* Find max field name width */
    for (i = 0; i < nf; i++) {
        fi = get_field_index(bs, i);
        int nlen = strlen(db->fields[fi].name);
        if (nlen > max_name_w) max_name_w = nlen;
    }

    for (r = 0; r < nf && r < bs->data_rows; r++) {
        char display[256];
        int w;
        fi = get_field_index(bs, r);
        get_field_display(db, fi, display, sizeof(display));
        trim_right(display);
        w = cmd_field_display_width(db, fi);

        term_gotoxy(r + 3, 1);
        term_clear(1);  /* clear to end of line */
        {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%*s: ", max_name_w, db->fields[fi].name);
            term_puts(tmp);
        }

        if (r == bs->cur_field)
            term_set_attr(7);
        {
            int dlen = strlen(display);
            int j;
            for (j = 0; j < w; j++) {
                if (j < dlen) term_putc(display[j]);
                else term_putc(' ');
            }
        }
        if (r == bs->cur_field)
            term_set_attr(0);
    }
    /* Clear remaining data rows */
    for (; r < bs->data_rows; r++) {
        term_gotoxy(r + 3, 1);
        term_clear(1);
    }
}

static void cmd_edit_impl(dbf_t *db, const char *args)
{
    browse_state_t bs;
    int nf;

    memset(&bs, 0, sizeof(bs));
    bs.db = db;
    bs.is_edit = 1;
    bs.field_count = 0; /* all fields */

    term_get_size(&bs.screen_rows, &bs.screen_cols);
    if (bs.screen_rows < 5) bs.screen_rows = 24;
    if (bs.screen_cols < 20) bs.screen_cols = 80;
    bs.data_rows = bs.screen_rows - 3;

    /* Parse optional record number */
    if (args && args[0]) {
        const char *p = skip_ws(args);
        if (*p >= '0' && *p <= '9') {
            uint32_t recno = (uint32_t)atoi(p);
            if (recno >= 1 && recno <= db->record_count)
                dbf_read_record(db, recno);
        }
    }

    /* Position at first valid record if needed */
    if (db->current_record == 0 || db->record_count == 0) {
        if (nav_top(&bs) != 0) {
            prog_error(ERR_RECORD_RANGE, "No records");
            return;
        }
    }
    cmd_follow_relations();

    term_set_raw(1);
    term_clear(0);

    nf = total_fields(&bs);

    for (;;) {
        int key;
        term_begin_update();
        edit_paint_header(&bs);
        edit_paint_record(&bs);
        edit_paint_help(&bs);
        term_end_update();

        key = read_dbase_key();
        if (key < 0) break;

        if (key == 27 || key == 23) break; /* Esc or Ctrl-W */

        if (key == DBASE_KEY_UP) {
            if (bs.cur_field > 0) bs.cur_field--;
        } else if (key == DBASE_KEY_DOWN || key == 9) { /* Down or Tab */
            if (bs.cur_field < nf - 1) bs.cur_field++;
        } else if (key == DBASE_KEY_PGDN) {
            uint32_t save = db->current_record;
            if (nav_next(&bs) != 0)
                dbf_read_record(db, save);
            else
                cmd_follow_relations();
        } else if (key == DBASE_KEY_PGUP) {
            uint32_t save = db->current_record;
            if (nav_prev(&bs) != 0)
                dbf_read_record(db, save);
            else
                cmd_follow_relations();
        } else if (key == 21) { /* Ctrl-U: toggle delete */
            db->record_buf[0] = (db->record_buf[0] == '*') ? ' ' : '*';
            db->record_dirty = 1;
            dbf_flush_record(db);
        } else if (key == 13 || key == 10 || (key >= 32 && key < 127)) {
            /* Enter edit mode for current field */
            int fi = get_field_index(&bs, bs.cur_field);
            if (db->fields[fi].type == 'M') continue;
            {
                int max_name_w = 0;
                int i;
                for (i = 0; i < nf; i++) {
                    int idx = get_field_index(&bs, i);
                    int nl = strlen(db->fields[idx].name);
                    if (nl > max_name_w) max_name_w = nl;
                }
                edit_cell(&bs, fi, bs.cur_field + 3,
                          max_name_w + 3,
                          cmd_field_display_width(db, fi),
                          (key == 13 || key == 10) ? 0 : key);
            }
        }
    }

    term_set_attr(0);
    term_set_raw(0);
    term_clear(0);
    term_gotoxy(1, 1);
}
#endif /* HAS_TERM */

void cmd_edit(dbf_t *db, const char *args)
{
    cmd_ctx_setup();
    if (!dbf_is_open(db)) {
        printf("No database in use.\n");
        return;
    }
#if HAS_TERM
    if (!screen_term_available()) {
        printf("EDIT requires terminal mode.\n");
        return;
    }
    cmd_edit_impl(db, args);
#else
    (void)args;
    printf("EDIT requires terminal mode.\n");
#endif
}

/* ------------------------------------------------------------------ */
/*  BROWSE (spreadsheet grid view)                                     */
/* ------------------------------------------------------------------ */

#if HAS_TERM
static void compute_columns(browse_state_t *bs)
{
    int x = 1;
    int nf = total_fields(bs);
    bs->num_visible_cols = 0;
    {
        int i;
        for (i = bs->first_field; i < nf; i++) {
            int fi = get_field_index(bs, i);
            int w = cmd_field_display_width(bs->db, fi);
            if (x + w > bs->screen_cols && bs->num_visible_cols > 0) break;
            bs->col_x[bs->num_visible_cols] = x;
            bs->col_w[bs->num_visible_cols] = w;
            bs->num_visible_cols++;
            x += w + 1; /* +1 for column gap */
        }
    }
}

/* Fill visible_recnos from current position going forward.
   Returns number of records filled. */
static int fill_page_forward(browse_state_t *bs, int count)
{
    dbf_t *db = bs->db;
    int filled = 0;

    if (db->current_record == 0) {
        if (nav_top(bs) != 0) return 0;
    }

    /* First record is current */
    if (!cmd_skip_deleted(db->record_buf) && cmd_check_filter(db)) {
        bs->visible_recnos[filled++] = db->current_record;
    }

    while (filled < count) {
        uint32_t save = db->current_record;
        if (nav_next(bs) != 0) {
            dbf_read_record(db, save);
            break;
        }
        bs->visible_recnos[filled++] = db->current_record;
    }

    bs->visible_count = filled;
    return filled;
}

static void browse_paint_headers(browse_state_t *bs)
{
    int c;
    /* Row 1: field names */
    term_gotoxy(1, 1);
    term_set_attr(7);
    {
        int i;
        for (i = 0; i < bs->screen_cols; i++) term_putc(' ');
    }
    for (c = 0; c < bs->num_visible_cols; c++) {
        int fi = get_field_index(bs, bs->first_field + c);
        term_gotoxy(1, bs->col_x[c]);
        {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%-*.*s", bs->col_w[c], bs->col_w[c], bs->db->fields[fi].name);
            term_puts(tmp);
        }
    }
    term_set_attr(0);

    /* Row 2: separator line */
    term_gotoxy(2, 1);
    {
        int i;
        for (i = 0; i < bs->screen_cols; i++) term_putc('-');
    }
}

static void browse_paint_row(browse_state_t *bs, int row, int highlight_col)
{
    int c;
    int screen_row = row + 3;

    term_gotoxy(screen_row, 1);
    term_clear(1);

    if (row >= bs->visible_count) return;

    dbf_read_record(bs->db, bs->visible_recnos[row]);

    for (c = 0; c < bs->num_visible_cols; c++) {
        int fi = get_field_index(bs, bs->first_field + c);
        char display[256];
        int dlen, j;

        get_field_display(bs->db, fi, display, sizeof(display));
        trim_right(display);
        dlen = strlen(display);

        term_gotoxy(screen_row, bs->col_x[c]);
        if (row == bs->cur_row && c == (bs->cur_field - bs->first_field))
            term_set_attr(7);

        for (j = 0; j < bs->col_w[c]; j++) {
            if (j < dlen) term_putc(display[j]);
            else term_putc(' ');
        }

        if (row == bs->cur_row && c == (bs->cur_field - bs->first_field))
            term_set_attr(0);
    }
    (void)highlight_col;
}

static void browse_paint_all_rows(browse_state_t *bs)
{
    int r;
    for (r = 0; r < bs->data_rows; r++)
        browse_paint_row(bs, r, -1);
}

static void browse_paint_status(browse_state_t *bs)
{
    dbf_t *db = bs->db;
    int is_del;
    char status[128];
    int len, i;
    int fi;

    /* Restore current record for status display */
    if (bs->visible_count > 0)
        dbf_read_record(db, bs->visible_recnos[bs->cur_row]);

    is_del = (db->record_buf[0] == '*');
    fi = get_field_index(bs, bs->cur_field);

    snprintf(status, sizeof(status), " Record: %u/%u    %-10s%s",
             db->current_record, db->record_count,
             db->fields[fi].name,
             is_del ? "  *Del*" : "");

    term_gotoxy(bs->screen_rows, 1);
    term_set_attr(7);
    len = strlen(status);
    term_puts(status);
    for (i = len; i < bs->screen_cols; i++) term_putc(' ');
    term_set_attr(0);
}

static void browse_refill_at_top(browse_state_t *bs)
{
    /* Position to first visible record and refill */
    if (bs->visible_count > 0)
        dbf_read_record(bs->db, bs->visible_recnos[0]);
    fill_page_forward(bs, bs->data_rows);
    if (bs->cur_row >= bs->visible_count)
        bs->cur_row = bs->visible_count > 0 ? bs->visible_count - 1 : 0;
}

static void cmd_browse_impl(dbf_t *db, const char *args)
{
    browse_state_t bs;
    int nf;

    memset(&bs, 0, sizeof(bs));
    bs.db = db;
    bs.is_edit = 0;

    term_get_size(&bs.screen_rows, &bs.screen_cols);
    if (bs.screen_rows < 5) bs.screen_rows = 24;
    if (bs.screen_cols < 20) bs.screen_cols = 80;
    bs.data_rows = bs.screen_rows - 3;
    if (bs.data_rows > BROWSE_MAX_ROWS)
        bs.data_rows = BROWSE_MAX_ROWS;

    /* Parse FIELDS clause */
    if (args && args[0]) {
        const char *p = skip_ws(args);
        char upper[256];
        str_copy(upper, p, sizeof(upper));
        str_upper(upper);
        if (strncmp(upper, "FIELDS", 6) == 0 &&
            (upper[6] == ' ' || upper[6] == '\0')) {
            p = skip_ws(p + 6);
            bs.field_count = 0;
            while (*p && bs.field_count < DBF_MAX_FIELDS) {
                char fname[32];
                int fi;
                int k = 0;
                while (*p && *p != ',' && *p != ' ' && k < 30)
                    fname[k++] = *p++;
                fname[k] = '\0';
                str_upper(fname);
                fi = dbf_find_field(db, fname);
                if (fi >= 0)
                    bs.field_list[bs.field_count++] = fi;
                while (*p == ',' || *p == ' ') p++;
            }
            if (bs.field_count == 0) {
                printf("No valid fields specified.\n");
                return;
            }
        }
    }

    nf = total_fields(&bs);
    if (nf == 0) {
        printf("No fields.\n");
        return;
    }

    /* Position at first record */
    if (db->current_record == 0) {
        if (nav_top(&bs) != 0) {
            prog_error(ERR_RECORD_RANGE, "No records");
            return;
        }
    }
    cmd_follow_relations();

    /* Fill initial page */
    fill_page_forward(&bs, bs.data_rows);
    if (bs.visible_count == 0) {
        prog_error(ERR_RECORD_RANGE, "No records");
        return;
    }

    compute_columns(&bs);

    term_set_raw(1);
    term_clear(0);
    term_begin_update();
    browse_paint_headers(&bs);
    browse_paint_all_rows(&bs);
    browse_paint_status(&bs);
    term_end_update();

    for (;;) {
        int key = read_dbase_key();
        if (key < 0) break;
        if (key == 27 || key == 23) break; /* Esc or Ctrl-W */

        if (key == DBASE_KEY_UP) {
            if (bs.cur_row > 0) {
                int old_row = bs.cur_row;
                bs.cur_row--;
                term_begin_update();
                browse_paint_row(&bs, old_row, -1);
                browse_paint_row(&bs, bs.cur_row, -1);
                browse_paint_status(&bs);
                term_end_update();
            } else {
                /* Scroll up: try to get a previous record */
                uint32_t first = bs.visible_recnos[0];
                dbf_read_record(db, first);
                if (nav_prev(&bs) == 0) {
                    /* Shift all records down, insert new one at top */
                    int j;
                    if (bs.visible_count >= bs.data_rows) bs.visible_count = bs.data_rows;
                    for (j = bs.visible_count - 1; j > 0; j--)
                        bs.visible_recnos[j] = bs.visible_recnos[j - 1];
                    bs.visible_recnos[0] = db->current_record;
                    if (bs.visible_count < bs.data_rows) bs.visible_count++;
                    term_begin_update();
                    browse_paint_all_rows(&bs);
                    browse_paint_status(&bs);
                    term_end_update();
                } else {
                    dbf_read_record(db, first);
                }
            }
        } else if (key == DBASE_KEY_DOWN) {
            if (bs.cur_row < bs.visible_count - 1) {
                int old_row = bs.cur_row;
                bs.cur_row++;
                term_begin_update();
                browse_paint_row(&bs, old_row, -1);
                browse_paint_row(&bs, bs.cur_row, -1);
                browse_paint_status(&bs);
                term_end_update();
            } else {
                /* Scroll down: try to get next record */
                uint32_t last = bs.visible_recnos[bs.visible_count - 1];
                dbf_read_record(db, last);
                if (nav_next(&bs) == 0) {
                    uint32_t new_rec = db->current_record;
                    /* Shift up and add at bottom */
                    int j;
                    for (j = 0; j < bs.visible_count - 1; j++)
                        bs.visible_recnos[j] = bs.visible_recnos[j + 1];
                    bs.visible_recnos[bs.visible_count - 1] = new_rec;
                    term_begin_update();
                    browse_paint_all_rows(&bs);
                    browse_paint_status(&bs);
                    term_end_update();
                } else {
                    dbf_read_record(db, last);
                }
            }
        } else if (key == DBASE_KEY_LEFT || key == DBASE_KEY_SHIFT_TAB) {
            if (bs.cur_field > 0) {
                int old_col = bs.cur_field - bs.first_field;
                bs.cur_field--;
                term_begin_update();
                if (bs.cur_field < bs.first_field) {
                    bs.first_field = bs.cur_field;
                    compute_columns(&bs);
                    browse_paint_headers(&bs);
                    browse_paint_all_rows(&bs);
                } else {
                    browse_paint_row(&bs, bs.cur_row, old_col);
                    browse_paint_row(&bs, bs.cur_row, -1);
                }
                browse_paint_status(&bs);
                term_end_update();
            }
        } else if (key == DBASE_KEY_RIGHT || key == 9) { /* Right or Tab */
            if (bs.cur_field < nf - 1) {
                int old_col = bs.cur_field - bs.first_field;
                bs.cur_field++;
                term_begin_update();
                if (bs.cur_field >= bs.first_field + bs.num_visible_cols) {
                    bs.first_field++;
                    compute_columns(&bs);
                    browse_paint_headers(&bs);
                    browse_paint_all_rows(&bs);
                } else {
                    browse_paint_row(&bs, bs.cur_row, old_col);
                    browse_paint_row(&bs, bs.cur_row, -1);
                }
                browse_paint_status(&bs);
                term_end_update();
            }
        } else if (key == DBASE_KEY_HOME) {
            if (bs.cur_field != 0 || bs.first_field != 0) {
                bs.cur_field = 0;
                bs.first_field = 0;
                compute_columns(&bs);
                term_begin_update();
                browse_paint_headers(&bs);
                browse_paint_all_rows(&bs);
                browse_paint_status(&bs);
                term_end_update();
            }
        } else if (key == DBASE_KEY_END) {
            if (bs.cur_field != nf - 1) {
                bs.cur_field = nf - 1;
                /* Adjust first_field so cur_field is visible */
                if (bs.cur_field >= bs.first_field + bs.num_visible_cols) {
                    bs.first_field = bs.cur_field;
                    /* Walk back to fit as many columns as possible */
                    {
                        int total_w = 0;
                        int ff;
                        for (ff = bs.cur_field; ff >= 0; ff--) {
                            int fi = get_field_index(&bs, ff);
                            int w = cmd_field_display_width(db, fi) + 1;
                            if (total_w + w > bs.screen_cols && ff < bs.cur_field) break;
                            total_w += w;
                        }
                        bs.first_field = ff + 1;
                    }
                    compute_columns(&bs);
                    term_begin_update();
                    browse_paint_headers(&bs);
                    browse_paint_all_rows(&bs);
                    browse_paint_status(&bs);
                    term_end_update();
                } else {
                    term_begin_update();
                    browse_paint_status(&bs);
                    term_end_update();
                }
            }
        } else if (key == DBASE_KEY_PGDN) {
            /* Page down */
            if (bs.visible_count > 0) {
                uint32_t last = bs.visible_recnos[bs.visible_count - 1];
                dbf_read_record(db, last);
                if (nav_next(&bs) == 0) {
                    fill_page_forward(&bs, bs.data_rows);
                    bs.cur_row = 0;
                    term_begin_update();
                    browse_paint_all_rows(&bs);
                    browse_paint_status(&bs);
                    term_end_update();
                } else {
                    /* Already at end - move cursor to last row */
                    int old_row = bs.cur_row;
                    bs.cur_row = bs.visible_count - 1;
                    if (old_row != bs.cur_row) {
                        term_begin_update();
                        browse_paint_row(&bs, old_row, -1);
                        browse_paint_row(&bs, bs.cur_row, -1);
                        browse_paint_status(&bs);
                        term_end_update();
                    }
                    dbf_read_record(db, last);
                }
            }
        } else if (key == DBASE_KEY_PGUP) {
            /* Page up: walk backward from first visible */
            if (bs.visible_count > 0) {
                uint32_t first = bs.visible_recnos[0];
                int count = bs.data_rows;
                int found = 0;
                /* Walk backward to find start of previous page */
                dbf_read_record(db, first);
                {
                    int i;
                    for (i = 0; i < count; i++) {
                        if (nav_prev(&bs) != 0) break;
                        found++;
                    }
                }
                if (found > 0) {
                    fill_page_forward(&bs, bs.data_rows);
                    bs.cur_row = 0;
                    term_begin_update();
                    browse_paint_all_rows(&bs);
                    browse_paint_status(&bs);
                    term_end_update();
                } else {
                    /* Already at top */
                    int old_row = bs.cur_row;
                    bs.cur_row = 0;
                    if (old_row != 0) {
                        term_begin_update();
                        browse_paint_row(&bs, old_row, -1);
                        browse_paint_row(&bs, 0, -1);
                        browse_paint_status(&bs);
                        term_end_update();
                    }
                    dbf_read_record(db, first);
                }
            }
        } else if (key == 21) { /* Ctrl-U: toggle delete */
            if (bs.visible_count > 0) {
                dbf_read_record(db, bs.visible_recnos[bs.cur_row]);
                db->record_buf[0] = (db->record_buf[0] == '*') ? ' ' : '*';
                db->record_dirty = 1;
                dbf_flush_record(db);
                term_begin_update();
                browse_paint_row(&bs, bs.cur_row, -1);
                browse_paint_status(&bs);
                term_end_update();
            }
        } else if (key == 14) { /* Ctrl-N: append blank */
            char old_keys[MAX_INDEXES][MAX_INDEX_KEY + 1];
            dbf_append_blank(db);
            cmd_indexes_insert_current(db);
            /* Refill page to include new record at end */
            fill_page_forward(&bs, bs.data_rows);
            /* Try to show new record: go to bottom */
            nav_bottom(&bs);
            fill_page_forward(&bs, bs.data_rows);
            bs.cur_row = bs.visible_count - 1;
            term_begin_update();
            browse_paint_all_rows(&bs);
            browse_paint_status(&bs);
            term_end_update();
            (void)old_keys;
        } else if (key == 13 || key == 10 || (key >= 32 && key < 127)) {
            /* Edit current cell */
            if (bs.visible_count > 0) {
                int col_in_view = bs.cur_field - bs.first_field;
                int fi = get_field_index(&bs, bs.cur_field);
                if (col_in_view >= 0 && col_in_view < bs.num_visible_cols &&
                    db->fields[fi].type != 'M') {
                    dbf_read_record(db, bs.visible_recnos[bs.cur_row]);
                    edit_cell(&bs, fi,
                              bs.cur_row + 3,
                              bs.col_x[col_in_view],
                              bs.col_w[col_in_view],
                              (key == 13 || key == 10) ? 0 : key);
                    term_begin_update();
                    browse_paint_row(&bs, bs.cur_row, -1);
                    browse_paint_status(&bs);
                    term_end_update();
                }
            }
        }


    }

    /* Restore current record position */
    if (bs.visible_count > 0)
        dbf_read_record(db, bs.visible_recnos[bs.cur_row]);

    term_set_attr(0);
    term_set_raw(0);
    term_clear(0);
    term_gotoxy(1, 1);
}
#endif /* HAS_TERM */

void cmd_browse(dbf_t *db, const char *args)
{
    cmd_ctx_setup();
    if (!dbf_is_open(db)) {
        printf("No database in use.\n");
        return;
    }
#if HAS_TERM
    if (!screen_term_available()) {
        printf("BROWSE requires terminal mode.\n");
        return;
    }
    cmd_browse_impl(db, args);
#else
    (void)args;
    printf("BROWSE requires terminal mode.\n");
#endif
}
