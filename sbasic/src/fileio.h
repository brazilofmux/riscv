#ifndef SBASIC_FILEIO_H
#define SBASIC_FILEIO_H

#include "error.h"
#include <stdio.h>

#define MAX_FILE_HANDLES 16

typedef enum {
    FMODE_NONE,
    FMODE_INPUT,
    FMODE_OUTPUT,
    FMODE_APPEND,
    FMODE_BINARY,
    FMODE_RANDOM,
} file_mode_t;

typedef struct {
    FILE *fp;
    file_mode_t mode;
    int in_use;
    int col;        /* current column position (0-based) for TAB */
    int reclen;     /* record length for RANDOM mode (0 = N/A) */
} file_handle_t;

/* Initialize file handle table */
void fileio_init(void);

/* Close all open files */
void fileio_close_all(void);

/* OPEN file$ FOR mode AS #n */
error_t fileio_open(const char *filename, file_mode_t mode, int handle);

/* CLOSE #n (0 = close all) */
error_t fileio_close(int handle);

/* Get FILE* for a handle (returns NULL if not open) */
FILE *fileio_get(int handle);

/* Check if handle is open for the given mode */
int fileio_is_open(int handle, file_mode_t required_mode);

/* EOF(n) */
int fileio_eof(int handle);

/* FREEFILE - next available handle */
int fileio_freefile(void);

/* NAME old$ AS new$ */
error_t fileio_rename(const char *oldname, const char *newname);

/* Get pointer to column counter for a handle (for TAB tracking) */
int *fileio_get_col_ptr(int handle);

/* KILL file$ */
error_t fileio_kill(const char *filename);

/* Set record length for a handle (RANDOM mode) */
void fileio_set_reclen(int handle, int reclen);

/* Get file mode for a handle */
file_mode_t fileio_get_mode(int handle);

/* Get record length for a handle */
int fileio_get_reclen(int handle);

#endif
