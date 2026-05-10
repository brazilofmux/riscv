#ifndef _DIRENT_H
#define _DIRENT_H

#include <stdint.h>
#include <stddef.h>

/* File-type constants for d_type, matching POSIX DT_* values. */
#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK     10
#define DT_SOCK    12

struct dirent {
    uint64_t      d_ino;        /* inode number */
    unsigned char d_type;       /* DT_REG, DT_DIR, etc. */
    char          d_name[256];  /* NUL-terminated filename */
};

typedef struct DIR DIR;

DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
int            dirfd(DIR *dirp);
void           rewinddir(DIR *dirp);

#endif /* _DIRENT_H */
