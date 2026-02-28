#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "dbf.h"
#include "area.h"
#include "util.h"
#include "util.h"

#define DBF_CACHE_RECORDS 32

void dbf_init(dbf_t *db) {
    memset(db, 0, sizeof(*db));
    db->memo_fp = NULL;
    db->next_memo_block = 0;
    db->has_memo = 0;
}

void dbf_cache_invalidate(dbf_t *db) {
    db->cache_start = 0;
    db->cache_count = 0;
}

static void dbf_cache_free(dbf_t *db) {
    if (db->cache_buf) {
        free(db->cache_buf);
        db->cache_buf = NULL;
    }
    db->cache_capacity = 0;
    dbf_cache_invalidate(db);
}

static void dbf_cache_init(dbf_t *db) {
    size_t bytes;

    dbf_cache_free(db);

    if (db->record_size == 0)
        return;

    db->cache_capacity = DBF_CACHE_RECORDS;
    bytes = (size_t)db->record_size * (size_t)db->cache_capacity;
    db->cache_buf = (char *)malloc(bytes);
    if (!db->cache_buf) {
        db->cache_capacity = 0;
        return;
    }
    dbf_cache_invalidate(db);
}

static void write_le16(unsigned char *p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

static void write_le32(unsigned char *p, uint32_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

static uint16_t read_le16(const unsigned char *p) {
    return p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const unsigned char *p) {
    return p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int dbf_create(const char *filename, const dbf_field_t *fields, int nfields) {
    FILE *fp;
    unsigned char hdr[32];
    unsigned char fdesc[32];
    unsigned char term;
    int i;
    uint16_t hdr_size, rec_size;
    char norm_path[64];

    str_copy(norm_path, filename, sizeof(norm_path));
    path_normalize(norm_path);

    if (nfields < 1 || nfields > DBF_MAX_FIELDS) return -1;

    fp = fopen(norm_path, "w+b");
    if (!fp) return -1;

    hdr_size = 32 + nfields * 32 + 1;
    rec_size = 1; /* deletion flag */
    for (i = 0; i < nfields; i++)
        rec_size += fields[i].length;

    /* Check for memo fields */
    {
        int has_memo = 0;
        for (i = 0; i < nfields; i++)
            if (fields[i].type == 'M') has_memo = 1;

        /* Write header */
        memset(hdr, 0, 32);
        hdr[0] = has_memo ? 0x83 : 0x03; /* 0x83 = dBase III with memo */
    }
    {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        hdr[1] = (unsigned char)(t ? t->tm_year : 126);  /* year - 1900 */
        hdr[2] = (unsigned char)(t ? t->tm_mon + 1 : 1); /* month 1-12 */
        hdr[3] = (unsigned char)(t ? t->tm_mday : 1);    /* day 1-31 */
    }
    write_le32(hdr + 4, 0);          /* record count = 0 */
    write_le16(hdr + 8, hdr_size);
    write_le16(hdr + 10, rec_size);
    fwrite(hdr, 1, 32, fp);

    /* Write field descriptors */
    for (i = 0; i < nfields; i++) {
        memset(fdesc, 0, 32);
        str_copy((char *)fdesc, fields[i].name, 11);
        fdesc[11] = fields[i].type;
        fdesc[16] = fields[i].length;
        fdesc[17] = fields[i].decimals;
        fwrite(fdesc, 1, 32, fp);
    }

    /* Header terminator */
    term = 0x0D;
    fwrite(&term, 1, 1, fp);

    fclose(fp);
    return 0;
}

int dbf_open(dbf_t *db, const char *filename) {
    unsigned char hdr[32];
    unsigned char fdesc[32];
    int i, nfields;
    uint16_t offset;
    char norm_path[64];

    str_copy(norm_path, filename, sizeof(norm_path));
    path_normalize(norm_path);

    if (db->fp) dbf_close(db);

    db->fp = fopen(norm_path, "r+b");
    if (!db->fp) return -1;

    str_copy(db->filename, norm_path, sizeof(db->filename));

    /* Read header */
    if (fread(hdr, 1, 32, db->fp) != 32) {
        fclose(db->fp);
        db->fp = NULL;
        return -1;
    }

    db->record_count = read_le32(hdr + 4);
    db->header_size = read_le16(hdr + 8);
    db->record_size = read_le16(hdr + 10);

    /* Number of fields = (header_size - 32 - 1) / 32 */
    nfields = (db->header_size - 33) / 32;
    if (nfields < 0 || nfields > DBF_MAX_FIELDS) {
        fclose(db->fp);
        db->fp = NULL;
        return -1;
    }
    db->field_count = nfields;

    /* Read field descriptors */
    offset = 0;
    for (i = 0; i < nfields; i++) {
        if (fread(fdesc, 1, 32, db->fp) != 32) {
            fclose(db->fp);
            db->fp = NULL;
            return -1;
        }
        str_copy(db->fields[i].name, (char *)fdesc, DBF_MAX_FIELD_NAME);
        db->fields[i].type = fdesc[11];
        db->fields[i].length = fdesc[16];
        db->fields[i].decimals = fdesc[17];
        db->fields[i].offset = offset;
        offset += fdesc[16];
    }

    db->current_record = 0;
    db->record_dirty = 0;
    dbf_cache_init(db);

    /* Check for memo fields */
    db->has_memo = 0;
    for (i = 0; i < nfields; i++) {
        if (db->fields[i].type == 'M') {
            db->has_memo = 1;
            break;
        }
    }
    if (db->has_memo)
        dbf_memo_open(db);

    return 0;
}

void dbf_close(dbf_t *db) {
    if (!db->fp) return;
    if (db->record_dirty)
        dbf_flush_record(db);
    dbf_write_header_counts(db);
    dbf_memo_close(db);
    fclose(db->fp);
    db->fp = NULL;
    db->filename[0] = '\0';
    db->current_record = 0;
    db->record_dirty = 0;
    dbf_cache_free(db);
}

int dbf_is_open(const dbf_t *db) {
    return db->fp != NULL;
}

int dbf_append_blank(dbf_t *db) {
    long pos;

    if (!db->fp) return -1;

    /* Flush current record if dirty */
    if (db->record_dirty)
        dbf_flush_record(db);

    db->record_count++;
    db->current_record = db->record_count;

    /* Fill record buffer with spaces, deletion flag = ' ' */
    memset(db->record_buf, ' ', db->record_size);
    db->record_buf[db->record_size] = '\0';

    /* Seek to record position and write */
    pos = db->header_size + (long)(db->current_record - 1) * db->record_size;
    fseek(db->fp, pos, 0); /* SEEK_SET = 0 */
    fwrite(db->record_buf, 1, db->record_size, db->fp);
    fflush(db->fp);

    /* Update header */
    dbf_write_header_counts(db);

    db->record_dirty = 0;
    area_invalidate_all(db->filename);
    return 0;
}

int dbf_read_record(dbf_t *db, uint32_t recno) {
    long pos;
    uint32_t start;
    uint32_t available;
    int to_read;
    size_t bytes;

    if (!db->fp) return -1;
    if (recno < 1 || recno > db->record_count) return -1;

    /* Flush current if dirty */
    if (db->record_dirty)
        dbf_flush_record(db);

    if (db->cache_buf && db->cache_count > 0 &&
        recno >= db->cache_start &&
        recno < db->cache_start + (uint32_t)db->cache_count) {
        int idx = (int)(recno - db->cache_start);
        memcpy(db->record_buf, db->cache_buf + idx * db->record_size, db->record_size);
        db->record_buf[db->record_size] = '\0';
    } else if (db->cache_buf && db->cache_capacity > 0) {
        start = recno;
        available = db->record_count - start + 1;
        to_read = db->cache_capacity;
        if (to_read > (int)available) to_read = (int)available;
        bytes = (size_t)db->record_size * (size_t)to_read;

        pos = db->header_size + (long)(start - 1) * db->record_size;
        fseek(db->fp, pos, 0);
        if (fread(db->cache_buf, 1, bytes, db->fp) != bytes)
            return -1;
        db->cache_start = start;
        db->cache_count = to_read;

        memcpy(db->record_buf, db->cache_buf, db->record_size);
        db->record_buf[db->record_size] = '\0';
    } else {
        pos = db->header_size + (long)(recno - 1) * db->record_size;
        fseek(db->fp, pos, 0);
        if (fread(db->record_buf, 1, db->record_size, db->fp) != db->record_size)
            return -1;
        db->record_buf[db->record_size] = '\0';
    }

    db->current_record = recno;
    db->record_dirty = 0;
    return 0;
}

int dbf_flush_record(dbf_t *db) {
    long pos;

    if (!db->fp || !db->record_dirty || db->current_record == 0)
        return 0;

    pos = db->header_size + (long)(db->current_record - 1) * db->record_size;
    fseek(db->fp, pos, 0);
    fwrite(db->record_buf, 1, db->record_size, db->fp);
    fflush(db->fp);
    db->record_dirty = 0;
    area_invalidate_all(db->filename);
    return 0;
}

int dbf_find_field(const dbf_t *db, const char *name) {
    int i;
    for (i = 0; i < db->field_count; i++) {
        if (str_icmp(db->fields[i].name, name) == 0)
            return i;
    }
    return -1;
}

int dbf_get_field_raw(const dbf_t *db, int idx, char *buf, int bufsize) {
    int len;

    if (idx < 0 || idx >= db->field_count) return -1;
    if (db->current_record == 0) return -1;

    len = db->fields[idx].length;
    if (len >= bufsize) len = bufsize - 1;

    /* +1 to skip deletion flag byte */
    memcpy(buf, db->record_buf + 1 + db->fields[idx].offset, len);
    buf[len] = '\0';
    return 0;
}

int dbf_set_field_raw(dbf_t *db, int idx, const char *value) {
    int len, vlen;

    if (idx < 0 || idx >= db->field_count) return -1;
    if (db->current_record == 0) return -1;

    len = db->fields[idx].length;
    vlen = strlen(value);
    if (vlen > len) vlen = len;

    /* Copy value, pad with spaces */
    memcpy(db->record_buf + 1 + db->fields[idx].offset, value, vlen);
    if (vlen < len)
        memset(db->record_buf + 1 + db->fields[idx].offset + vlen, ' ', len - vlen);

    db->record_dirty = 1;
    dbf_cache_invalidate(db);
    return 0;
}

int dbf_write_header_counts(dbf_t *db) {
    unsigned char buf[8];

    if (!db->fp) return -1;

    fseek(db->fp, 4, 0);
    write_le32(buf, db->record_count);
    fwrite(buf, 1, 4, db->fp);
    fflush(db->fp);
    return 0;
}

/* ---- Memo (.DBT) support ---- */

#define MEMO_BLOCK_SIZE 512

static void derive_dbt_name(const char *dbf_name, char *dbt_name, int size) {
    int len;
    str_copy(dbt_name, dbf_name, size);
    len = strlen(dbt_name);
    if (len >= 4 && (str_icmp(dbt_name + len - 4, ".DBF") == 0)) {
        dbt_name[len - 3] = 'D';
        dbt_name[len - 2] = 'B';
        dbt_name[len - 1] = 'T';
    }
}

int dbf_memo_create(const char *dbf_filename) {
    char dbt_name[64];
    FILE *fp;
    unsigned char header[MEMO_BLOCK_SIZE];

    derive_dbt_name(dbf_filename, dbt_name, sizeof(dbt_name));

    fp = fopen(dbt_name, "w+b");
    if (!fp) return -1;

    memset(header, 0, MEMO_BLOCK_SIZE);
    /* Next available block = 1 (block 0 is the header) */
    write_le32(header, 1);
    fwrite(header, 1, MEMO_BLOCK_SIZE, fp);
    fclose(fp);
    return 0;
}

int dbf_memo_open(dbf_t *db) {
    char dbt_name[64];
    unsigned char hdr[4];

    derive_dbt_name(db->filename, dbt_name, sizeof(dbt_name));

    db->memo_fp = fopen(dbt_name, "r+b");
    if (!db->memo_fp) {
        printf("Warning: memo file %s not found. Memo fields will be empty.\n", dbt_name);
        return -1;
    }

    if (fread(hdr, 1, 4, db->memo_fp) != 4) {
        fclose(db->memo_fp);
        db->memo_fp = NULL;
        return -1;
    }

    db->next_memo_block = read_le32(hdr);
    return 0;
}

void dbf_memo_close(dbf_t *db) {
    if (!db->memo_fp) return;

    /* Write back next_memo_block to header */
    {
        unsigned char hdr[4];
        fseek(db->memo_fp, 0, 0);
        write_le32(hdr, db->next_memo_block);
        fwrite(hdr, 1, 4, db->memo_fp);
        fflush(db->memo_fp);
    }

    fclose(db->memo_fp);
    db->memo_fp = NULL;
}

int dbf_memo_read(dbf_t *db, int block, char *buf, int bufsize) {
    int pos, n, i;
    unsigned char chunk[MEMO_BLOCK_SIZE];

    if (block <= 0 || !db->memo_fp) {
        buf[0] = '\0';
        return 0;
    }

    n = 0;
    pos = block * MEMO_BLOCK_SIZE;
    fseek(db->memo_fp, pos, 0);

    for (;;) {
        int bytes_read = fread(chunk, 1, MEMO_BLOCK_SIZE, db->memo_fp);
        if (bytes_read <= 0) break;

        for (i = 0; i < bytes_read; i++) {
            /* Check for 0x1A terminator (single or double) */
            if (chunk[i] == 0x1A) {
                buf[n] = '\0';
                return 0;
            }
            /* Convert soft CR (0x8D) to regular CR (0x0D) */
            if (n < bufsize - 1) {
                buf[n++] = (chunk[i] == 0x8D) ? 0x0D : chunk[i];
            } else {
                /* Buffer full — stop reading */
                buf[n] = '\0';
                return 0;
            }
        }
    }

    buf[n] = '\0';
    return 0;
}

int dbf_memo_write(dbf_t *db, const char *text, int len) {
    int start_block;
    int total, blocks_used;
    unsigned char pad[MEMO_BLOCK_SIZE];
    int remainder;

    if (!db->memo_fp) return -1;

    start_block = db->next_memo_block;

    /* Seek to write position */
    fseek(db->memo_fp, (long)start_block * MEMO_BLOCK_SIZE, 0);

    /* Write text */
    fwrite(text, 1, len, db->memo_fp);

    /* Write 0x1A 0x1A terminator */
    fwrite("\x1A\x1A", 1, 2, db->memo_fp);

    /* Zero-pad to block boundary */
    total = len + 2;
    remainder = total % MEMO_BLOCK_SIZE;
    if (remainder != 0) {
        int pad_len = MEMO_BLOCK_SIZE - remainder;
        memset(pad, 0, pad_len);
        fwrite(pad, 1, pad_len, db->memo_fp);
    }

    /* Compute blocks used */
    blocks_used = (total + MEMO_BLOCK_SIZE - 1) / MEMO_BLOCK_SIZE;
    db->next_memo_block = start_block + blocks_used;

    /* Flush header with updated next_memo_block */
    {
        unsigned char hdr[4];
        fseek(db->memo_fp, 0, 0);
        write_le32(hdr, db->next_memo_block);
        fwrite(hdr, 1, 4, db->memo_fp);
        fflush(db->memo_fp);
    }

    return start_block;
}

int dbf_memo_snapshot(dbf_t *db, memo_snapshot_t *snap) {
    long size;
    if (!snap) return -1;
    snap->valid = 0;
    if (!db || !db->memo_fp) return -1;
    fflush(db->memo_fp);
    fseek(db->memo_fp, 0, SEEK_END);
    size = ftell(db->memo_fp);
    if (size < 0) return -1;
    snap->file_size = size;
    snap->next_block = db->next_memo_block;
    snap->valid = 1;
    return 0;
}

int dbf_memo_restore(dbf_t *db, const memo_snapshot_t *snap) {
    unsigned char hdr[4];
    if (!db || !db->memo_fp || !snap || !snap->valid) return -1;
    fflush(db->memo_fp);
    {
        int fd = fileno(db->memo_fp);
        if (fd >= 0) {
            if (ftruncate(fd, snap->file_size) != 0) return -1;
        }
    }
    db->next_memo_block = snap->next_block;
    fseek(db->memo_fp, 0, 0);
    write_le32(hdr, db->next_memo_block);
    fwrite(hdr, 1, 4, db->memo_fp);
    fflush(db->memo_fp);
    return 0;
}
