#include <sys/stat.h>

/* Linux RISC-V syscall numbers */
#define SYS_mkdirat  34
#define SYS_fstatat  79
#define AT_FDCWD     -100

static inline long ecall6(long n, long a0, long a1, long a2, long a3, long a4, long a5) {
    register long ra0 __asm__("a0") = a0;
    register long ra1 __asm__("a1") = a1;
    register long ra2 __asm__("a2") = a2;
    register long ra3 __asm__("a3") = a3;
    register long ra4 __asm__("a4") = a4;
    register long ra5 __asm__("a5") = a5;
    register long ra7 __asm__("a7") = n;
    __asm__ volatile ("ecall" : "+r"(ra0)
        : "r"(ra1), "r"(ra2), "r"(ra3), "r"(ra4), "r"(ra5), "r"(ra7)
        : "memory");
    return ra0;
}

int stat(const char *path, struct stat *buf) {
    return (int)ecall6(SYS_fstatat, AT_FDCWD, (long)path, (long)buf, 0, 0, 0);
}

int fstat(int fd, struct stat *buf) {
    /* ECALL 80 is our existing fstat stub; use fstatat with empty path instead */
    return (int)ecall6(SYS_fstatat, fd, (long)"", (long)buf, 0x1000 /* AT_EMPTY_PATH */, 0, 0);
}

int mkdir(const char *pathname, mode_t mode) {
    return (int)ecall6(SYS_mkdirat, AT_FDCWD, (long)pathname, (long)mode, 0, 0, 0);
}
