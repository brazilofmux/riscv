/*
 * Test program for RAS and intrinsics.
 * Has recursive calls, memcpy/memset, and strlen.
 */
#include <stdint.h>
#include <stddef.h>

/* Bare-metal I/O */
static inline long sys_write(int fd, const void *buf, unsigned long len) {
    register long a0 __asm__("a0") = fd;
    register long a1 __asm__("a1") = (long)buf;
    register long a2 __asm__("a2") = (long)len;
    register long a7 __asm__("a7") = 64;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
    return a0;
}

static inline void sys_exit(int code) {
    register long a0 __asm__("a0") = code;
    register long a7 __asm__("a7") = 93;
    __asm__ volatile ("ecall" : : "r"(a0), "r"(a7));
    __builtin_unreachable();
}

/* String/memory functions (will be intercepted by intrinsics) */
void *memcpy(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memset(void *s, int c, size_t n) {
    char *p = s;
    while (n--) *p++ = (char)c;
    return s;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

/* Recursive fibonacci */
__attribute__((noinline))
static uint32_t fib(uint32_t n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

static void print_str(const char *s) {
    sys_write(1, s, strlen(s));
}

static void print_hex(uint32_t val) {
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 9; i >= 2; i--) {
        int nib = val & 0xF;
        buf[i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
        val >>= 4;
    }
    buf[10] = '\n';
    sys_write(1, buf, 11);
}

void _start(void) {
    /* Test recursive calls (exercises RAS) */
    uint32_t f = fib(30);
    print_str("fib(30)=");
    print_hex(f);  /* expect 0x000d2f00 = 832040 */

    /* Test memcpy */
    char src[64], dst[64];
    for (int i = 0; i < 64; i++) src[i] = (char)(i + 'A');
    memcpy(dst, src, 64);
    uint32_t sum = 0;
    for (int i = 0; i < 64; i++) sum += (uint32_t)(unsigned char)dst[i];
    print_str("memcpy= ");
    print_hex(sum);  /* 64*'A' + 0+1+...+63 = 64*65 + 2016 = 4160+2016 = 6176 = 0x00001820 */

    /* Test memset */
    memset(dst, 0x42, 64);
    sum = 0;
    for (int i = 0; i < 64; i++) sum += (uint32_t)(unsigned char)dst[i];
    print_str("memset= ");
    print_hex(sum);  /* 64 * 0x42 = 64 * 66 = 4224 = 0x00001080 */

    /* Test strlen */
    const char *msg = "Hello RAS!";
    uint32_t len = (uint32_t)strlen(msg);
    print_str("strlen= ");
    print_hex(len);  /* 10 = 0x0000000a */

    sys_exit(0);
}
