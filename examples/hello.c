/*
 * Bare-metal hello world for RV32IM.
 * Uses ECALL with Linux-style syscall numbers for basic I/O.
 *   a7 = syscall number (64=write, 93=exit)
 *   a0-a2 = arguments
 */

static inline long ecall_write(int fd, const void *buf, unsigned long len) {
    register long a0 __asm__("a0") = fd;
    register long a1 __asm__("a1") = (long)buf;
    register long a2 __asm__("a2") = (long)len;
    register long a7 __asm__("a7") = 64;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
    return a0;
}

static inline void ecall_exit(int code) {
    register long a0 __asm__("a0") = code;
    register long a7 __asm__("a7") = 93;
    __asm__ volatile ("ecall" : : "r"(a0), "r"(a7));
    __builtin_unreachable();
}

static unsigned my_strlen(const char *s) {
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

void _start(void) {
    const char *msg = "Hello from RV32IM!\n";
    ecall_write(1, msg, my_strlen(msg));
    ecall_exit(0);
}
