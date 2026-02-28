#include "fileio.h"
#include <string.h>

static file_handle_t handles[MAX_FILE_HANDLES + 1]; /* 1-based indexing */

void fileio_init(void) {
    memset(handles, 0, sizeof(handles));
}

void fileio_close_all(void) {
    for (int i = 1; i <= MAX_FILE_HANDLES; i++) {
        if (handles[i].in_use && handles[i].fp) {
            fclose(handles[i].fp);
            handles[i].fp = NULL;
            handles[i].in_use = 0;
            handles[i].mode = FMODE_NONE;
        }
    }
}

error_t fileio_open(const char *filename, file_mode_t mode, int handle) {
    if (handle < 1 || handle > MAX_FILE_HANDLES)
        return ERR_ILLEGAL_FUNCTION_CALL;
    if (handles[handle].in_use)
        return ERR_FILE_ALREADY_OPEN;

    const char *fmode;
    switch (mode) {
        case FMODE_INPUT:  fmode = "r"; break;
        case FMODE_OUTPUT: fmode = "w"; break;
        case FMODE_APPEND: fmode = "a"; break;
        case FMODE_BINARY:
        case FMODE_RANDOM: fmode = "r+b"; break;
        default: return ERR_ILLEGAL_FUNCTION_CALL;
    }

    FILE *fp = fopen(filename, fmode);
    /* BINARY/RANDOM: if file doesn't exist, create it */
    if (!fp && (mode == FMODE_BINARY || mode == FMODE_RANDOM))
        fp = fopen(filename, "w+b");
    if (!fp) return ERR_FILE_NOT_FOUND;

    handles[handle].fp = fp;
    handles[handle].mode = mode;
    handles[handle].in_use = 1;
    handles[handle].col = 0;
    handles[handle].reclen = 0;
    return ERR_NONE;
}

error_t fileio_close(int handle) {
    if (handle == 0) {
        fileio_close_all();
        return ERR_NONE;
    }
    if (handle < 1 || handle > MAX_FILE_HANDLES)
        return ERR_ILLEGAL_FUNCTION_CALL;
    if (!handles[handle].in_use)
        return ERR_NONE; /* closing an already-closed handle is OK */

    if (handles[handle].fp) {
        fclose(handles[handle].fp);
        handles[handle].fp = NULL;
    }
    handles[handle].in_use = 0;
    handles[handle].mode = FMODE_NONE;
    handles[handle].col = 0;
    handles[handle].reclen = 0;
    return ERR_NONE;
}

FILE *fileio_get(int handle) {
    if (handle < 1 || handle > MAX_FILE_HANDLES) return NULL;
    if (!handles[handle].in_use) return NULL;
    return handles[handle].fp;
}

int fileio_is_open(int handle, file_mode_t required_mode) {
    if (handle < 1 || handle > MAX_FILE_HANDLES) return 0;
    if (!handles[handle].in_use) return 0;
    if (required_mode == FMODE_NONE) return 1;
    return handles[handle].mode == required_mode;
}

int fileio_eof(int handle) {
    if (handle < 1 || handle > MAX_FILE_HANDLES) return -1;
    if (!handles[handle].in_use || !handles[handle].fp) return -1;
    return feof(handles[handle].fp) ? -1 : 0;
}

int fileio_freefile(void) {
    for (int i = 1; i <= MAX_FILE_HANDLES; i++) {
        if (!handles[i].in_use)
            return i;
    }
    return 0; /* no free handles */
}

int *fileio_get_col_ptr(int handle) {
    if (handle < 1 || handle > MAX_FILE_HANDLES) return NULL;
    return &handles[handle].col;
}

error_t fileio_rename(const char *oldname, const char *newname) {
    if (rename(oldname, newname) != 0)
        return ERR_FILE_NOT_FOUND;
    return ERR_NONE;
}

error_t fileio_kill(const char *filename) {
    if (remove(filename) != 0)
        return ERR_FILE_NOT_FOUND;
    return ERR_NONE;
}

void fileio_set_reclen(int handle, int reclen) {
    if (handle < 1 || handle > MAX_FILE_HANDLES) return;
    handles[handle].reclen = reclen;
}

file_mode_t fileio_get_mode(int handle) {
    if (handle < 1 || handle > MAX_FILE_HANDLES) return FMODE_NONE;
    if (!handles[handle].in_use) return FMODE_NONE;
    return handles[handle].mode;
}

int fileio_get_reclen(int handle) {
    if (handle < 1 || handle > MAX_FILE_HANDLES) return 0;
    return handles[handle].reclen;
}
