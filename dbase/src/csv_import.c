
#line 1 "src/csv_import.rl"
/*
 * csv_import.rl — Streaming CSV parser using Ragel -G2 (goto-driven).
 *
 * Supports:
 *   - Standard CSV (comma-separated, double-quote delimited)
 *   - Custom quote character (WITH <char>)
 *   - BLANK mode (space-delimited, no quotes) — separate function, no Ragel
 *
 * Generate with:  ragel -G2 csv_import.rl -o csv_import.c
 */

#include <stdio.h>
#include <string.h>
#include "csv_import.h"

static void add_char(csv_parser_t *ps, unsigned char ch) {
    if (ps->field_len < CSV_MAX_FIELD_LEN - 1)
        ps->field_buf[ps->field_len++] = ch;
}

static void save_field(csv_parser_t *ps) {
    ps->field_buf[ps->field_len] = '\0';
    if (ps->row.num_fields < CSV_MAX_FIELDS) {
        memcpy(ps->row.fields[ps->row.num_fields],
               ps->field_buf, ps->field_len + 1);
        ps->row.num_fields++;
    }
    ps->field_len = 0;
}

/* Returns non-zero if callback requested stop */
static int emit_row(csv_parser_t *ps) {
    int rc = 0;
    ps->line_number++;
    if (ps->row.num_fields > 0) {
        if (ps->callback)
            rc = ps->callback(ps->line_number, &ps->row, ps->user_data);
    }
    ps->row.num_fields = 0;
    ps->field_len = 0;
    return rc;
}

/* ---- Ragel machine definition ---- */


#line 167 "src/csv_import.rl"



#line 49 "src/csv_import.c"
static const int csv_delimited_start = 0;
static const int csv_delimited_error = -1;

static const int csv_delimited_en_main_state = 1;
static const int csv_delimited_en_quoted_state = 2;
static const int csv_delimited_en_after_quote_state = 3;
static const int csv_delimited_en_unquoted_state = 4;
static const int csv_delimited_en_cr_state = 5;
static const int csv_delimited_en_cr_after_state = 6;
static const int csv_delimited_en_main = 0;


#line 170 "src/csv_import.rl"

void csv_parser_init(csv_parser_t *p_state, char quote_char, csv_row_cb cb, void *ud) {
    memset(p_state, 0, sizeof(*p_state));
    p_state->quote_char = quote_char;
    p_state->callback = cb;
    p_state->user_data = ud;
    p_state->line_number = 0;
    
#line 67 "src/csv_import.c"
	{
	( p_state->cs) = csv_delimited_start;
	}

#line 178 "src/csv_import.rl"
}

int csv_parser_feed(csv_parser_t *p_state, const char *data, int len, int is_eof) {
    const unsigned char *p = (const unsigned char *)data;
    const unsigned char *pe = p + len;
    const unsigned char *eof = is_eof ? pe : 0;

    /* Suppress unused-variable warnings for Ragel entry points */
    (void)csv_delimited_en_main;
    (void)csv_delimited_en_main_state;
    (void)csv_delimited_en_quoted_state;
    (void)csv_delimited_en_after_quote_state;
    (void)csv_delimited_en_unquoted_state;
    (void)csv_delimited_en_cr_state;
    (void)csv_delimited_en_cr_after_state;

    
#line 86 "src/csv_import.c"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( ( p_state->cs) )
	{
tr0:
#line 149 "src/csv_import.rl"
	{
        unsigned char ch = (*p);
        if (ch == '\n') {
            save_field(p_state);
            if (emit_row(p_state)) { p_state->error = 2; {p++; ( p_state->cs) = 0; goto _out;} }
        } else if (ch == '\r') {
            save_field(p_state);
            {goto st5;}
        } else if (ch == ',') {
            save_field(p_state);
        } else if (ch == (unsigned char)p_state->quote_char) {
            {goto st2;}
        } else {
            add_char(p_state, ch);
            {goto st4;}
        }
    }
	goto st0;
st0:
	if ( ++p == pe )
		goto _test_eof0;
case 0:
#line 114 "src/csv_import.c"
	goto tr0;
tr1:
#line 53 "src/csv_import.rl"
	{
        unsigned char ch = (*p);
        if (ch == '\n') {
            save_field(p_state);
            if (emit_row(p_state)) { p_state->error = 2; {p++; ( p_state->cs) = 1; goto _out;} }
        } else if (ch == '\r') {
            save_field(p_state);
            {goto st5;}
        } else if (ch == ',') {
            save_field(p_state);
        } else if (ch == (unsigned char)p_state->quote_char) {
            {goto st2;}
        } else {
            add_char(p_state, ch);
            {goto st4;}
        }
    }
	goto st1;
st1:
	if ( ++p == pe )
		goto _test_eof1;
case 1:
#line 138 "src/csv_import.c"
	goto tr1;
tr2:
#line 72 "src/csv_import.rl"
	{
        unsigned char ch = (*p);
        if (ch == (unsigned char)p_state->quote_char) {
            {goto st3;}
        } else {
            add_char(p_state, ch);
        }
    }
	goto st2;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
#line 153 "src/csv_import.c"
	goto tr2;
tr3:
#line 82 "src/csv_import.rl"
	{
        unsigned char ch = (*p);
        if (ch == (unsigned char)p_state->quote_char) {
            /* Escaped quote: "" -> " */
            add_char(p_state, ch);
            {goto st2;}
        } else if (ch == ',') {
            save_field(p_state);
            {goto st1;}
        } else if (ch == '\n') {
            save_field(p_state);
            if (emit_row(p_state)) { p_state->error = 2; {p++; ( p_state->cs) = 3; goto _out;} }
            {goto st1;}
        } else if (ch == '\r') {
            save_field(p_state);
            {goto st6;}
        } else {
            /* Char after closing quote — tolerate, treat as content */
            add_char(p_state, ch);
            {goto st4;}
        }
    }
	goto st3;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
#line 182 "src/csv_import.c"
	goto tr3;
tr4:
#line 106 "src/csv_import.rl"
	{
        unsigned char ch = (*p);
        if (ch == ',') {
            save_field(p_state);
            {goto st1;}
        } else if (ch == '\n') {
            save_field(p_state);
            if (emit_row(p_state)) { p_state->error = 2; {p++; ( p_state->cs) = 4; goto _out;} }
            {goto st1;}
        } else if (ch == '\r') {
            save_field(p_state);
            {goto st6;}
        } else {
            add_char(p_state, ch);
        }
    }
	goto st4;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
#line 205 "src/csv_import.c"
	goto tr4;
tr5:
#line 124 "src/csv_import.rl"
	{
        unsigned char ch = (*p);
        if (ch == '\n') {
            if (emit_row(p_state)) { p_state->error = 2; {p++; ( p_state->cs) = 5; goto _out;} }
            {goto st1;}
        } else {
            /* Bare \r — treat as row end, re-process this char */
            if (emit_row(p_state)) { p_state->error = 2; {p++; ( p_state->cs) = 5; goto _out;} }
            p--; {goto st1;}
        }
    }
	goto st5;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
#line 223 "src/csv_import.c"
	goto tr5;
tr6:
#line 137 "src/csv_import.rl"
	{
        unsigned char ch = (*p);
        if (ch == '\n') {
            if (emit_row(p_state)) { p_state->error = 2; {p++; ( p_state->cs) = 6; goto _out;} }
            {goto st1;}
        } else {
            if (emit_row(p_state)) { p_state->error = 2; {p++; ( p_state->cs) = 6; goto _out;} }
            p--; {goto st1;}
        }
    }
	goto st6;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
#line 240 "src/csv_import.c"
	goto tr6;
	}
	_test_eof0: ( p_state->cs) = 0; goto _test_eof; 
	_test_eof1: ( p_state->cs) = 1; goto _test_eof; 
	_test_eof2: ( p_state->cs) = 2; goto _test_eof; 
	_test_eof3: ( p_state->cs) = 3; goto _test_eof; 
	_test_eof4: ( p_state->cs) = 4; goto _test_eof; 
	_test_eof5: ( p_state->cs) = 5; goto _test_eof; 
	_test_eof6: ( p_state->cs) = 6; goto _test_eof; 

	_test_eof: {}
	_out: {}
	}

#line 195 "src/csv_import.rl"

    if (p_state->error)
        return -1;

    /* On EOF, flush any remaining field/row */
    if (is_eof) {
        if (p_state->field_len > 0 || p_state->row.num_fields > 0) {
            save_field(p_state);
            emit_row(p_state);
        }
    }

    return 0;
}

/* ---- BLANK mode: space-delimited line reader ---- */

int csv_import_blank(const char *filename, csv_row_cb cb, void *ud) {
    FILE *fp;
    char line[4096];
    int line_number = 0;
    static csv_row_t row;

    fp = fopen(filename, "rb");
    if (!fp) return -1;

    while (fgets(line, sizeof(line), fp)) {
        char *s = line;
        char *end;
        int len;

        /* Strip trailing \r\n */
        len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (len == 0) continue; /* skip blank lines */

        line_number++;
        row.num_fields = 0;

        /* Split on spaces */
        while (*s && row.num_fields < CSV_MAX_FIELDS) {
            while (*s == ' ') s++;
            if (*s == '\0') break;

            end = s;
            while (*end && *end != ' ') end++;

            len = end - s;
            if (len >= CSV_MAX_FIELD_LEN) len = CSV_MAX_FIELD_LEN - 1;
            memcpy(row.fields[row.num_fields], s, len);
            row.fields[row.num_fields][len] = '\0';
            row.num_fields++;

            s = end;
        }

        if (row.num_fields > 0) {
            if (cb(line_number, &row, ud))
                break;
        }
    }

    fclose(fp);
    return 0;
}
