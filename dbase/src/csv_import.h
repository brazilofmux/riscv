#ifndef CSV_IMPORT_H
#define CSV_IMPORT_H

#define CSV_MAX_FIELDS    128   /* matches DBF_MAX_FIELDS */
#define CSV_MAX_FIELD_LEN 256

typedef struct {
    int num_fields;
    char fields[CSV_MAX_FIELDS][CSV_MAX_FIELD_LEN];
} csv_row_t;

/* Return 0 to continue, non-zero to stop */
typedef int (*csv_row_cb)(int line, const csv_row_t *row, void *ud);

typedef struct {
    int cs;                                    /* Ragel state */
    char field_buf[CSV_MAX_FIELD_LEN];         /* current field accumulator */
    int field_len;
    csv_row_t row;                             /* completed fields for current row */
    int line_number;
    char quote_char;                           /* configurable: '"', '\'', etc. */
    csv_row_cb callback;
    void *user_data;
    int error;
} csv_parser_t;

void csv_parser_init(csv_parser_t *p, char quote_char, csv_row_cb cb, void *ud);
int  csv_parser_feed(csv_parser_t *p, const char *data, int len, int is_eof);

/* BLANK mode: simple space-delimited line reader (no Ragel needed) */
int csv_import_blank(const char *filename, csv_row_cb cb, void *ud);

#endif
