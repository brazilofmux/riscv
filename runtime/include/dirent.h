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

/* Layout matches what the host writes via ECALL 91:
 *   +0  uint64_t d_ino
 *   +8  unsigned char d_type
 *   +9  char d_name[256]
 * Sizeof rounds up to 272 on the guest (8-aligned). */
struct dirent {
    uint64_t      d_ino;
    unsigned char d_type;
    char          d_name[256];
};

typedef struct DIR {
    int handle;
} DIR;

DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
int            dirfd(DIR *dirp);
void           rewinddir(DIR *dirp);

#endif /* _DIRENT_H */
