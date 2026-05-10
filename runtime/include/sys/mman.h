/* sys/mman.h — POSIX memory-mapping declarations.
 *
 * The microcontroller profile has a flat memory model with no host
 * mmap support, so the mmap/munmap/mprotect implementations always
 * fail (return MAP_FAILED / -1). Header exists so code that references
 * these symbols can compile; anything that depends on real mmap will
 * detect the failure at runtime. */
#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include <stddef.h>

/* Linux protection bits — match the kernel ABI. */
#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

/* Linux flags (subset). */
#define MAP_SHARED     0x01
#define MAP_PRIVATE    0x02
#define MAP_FIXED      0x10
#define MAP_ANONYMOUS  0x20
#define MAP_ANON       MAP_ANONYMOUS

#define MAP_FAILED ((void *)-1)

void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
int   munmap(void *addr, size_t length);
int   mprotect(void *addr, size_t len, int prot);

#endif /* _SYS_MMAN_H */
