/* Edge cases for FCLASS and FCVT.W{,U}.{S,D} (RISC-V rtz semantics).
 *
 * Build:
 *   riscv64-unknown-elf-gcc -march=rv32imfd -mabi=ilp32d -O2 \
 *     -ffreestanding -nostdlib -Iruntime/include -T runtime/link.ld \
 *     runtime/crt0.o examples/test_fclass_fcvt.c runtime/libc.a -lgcc \
 *     -o examples/test_fclass_fcvt.elf
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>

static int passed, failed;

static void check_u32(uint32_t got, uint32_t exp, const char *name) {
    if (got == exp) {
        passed++;
    } else {
        printf("FAIL: %s got 0x%x exp 0x%x\n", name, got, exp);
        failed++;
    }
}

static uint32_t fclass_s(float f) {
    uint32_t r;
    __asm__ volatile ("fclass.s %0, %1" : "=r"(r) : "f"(f));
    return r;
}

static uint32_t fclass_d(double d) {
    uint32_t r;
    __asm__ volatile ("fclass.d %0, %1" : "=r"(r) : "f"(d));
    return r;
}

static int32_t fcvt_w_s(float f) {
    int32_t r;
    __asm__ volatile ("fcvt.w.s %0, %1, rtz" : "=r"(r) : "f"(f));
    return r;
}

static uint32_t fcvt_wu_s(float f) {
    uint32_t r;
    __asm__ volatile ("fcvt.wu.s %0, %1, rtz" : "=r"(r) : "f"(f));
    return r;
}

static int32_t fcvt_w_d(double d) {
    int32_t r;
    __asm__ volatile ("fcvt.w.d %0, %1, rtz" : "=r"(r) : "f"(d));
    return r;
}

static uint32_t fcvt_wu_d(double d) {
    uint32_t r;
    __asm__ volatile ("fcvt.wu.d %0, %1, rtz" : "=r"(r) : "f"(d));
    return r;
}

/* Quiet / signaling NaN bit patterns */
static float bits_f(uint32_t b) { float f; __builtin_memcpy(&f, &b, 4); return f; }
static double bits_d(uint64_t b) { double d; __builtin_memcpy(&d, &b, 8); return d; }

int main(void) {
    /* ---- FCLASS.S bit positions ----
     * 0:-inf 1:-norm 2:-sub 3:-0 4:+0 5:+sub 6:+norm 7:+inf 8:sNaN 9:qNaN */
    printf("--- fclass.s ---\n");
    check_u32(fclass_s(bits_f(0xFF800000u)), 1u << 0, "fclass -inf");
    check_u32(fclass_s(-2.0f),               1u << 1, "fclass -norm");
    check_u32(fclass_s(bits_f(0x80000001u)), 1u << 2, "fclass -sub");
    check_u32(fclass_s(-0.0f),               1u << 3, "fclass -0");
    check_u32(fclass_s(0.0f),                1u << 4, "fclass +0");
    check_u32(fclass_s(bits_f(0x00000001u)), 1u << 5, "fclass +sub");
    check_u32(fclass_s(2.0f),                1u << 6, "fclass +norm");
    check_u32(fclass_s(bits_f(0x7F800000u)), 1u << 7, "fclass +inf");
    check_u32(fclass_s(bits_f(0x7F800001u)), 1u << 8, "fclass sNaN");
    check_u32(fclass_s(bits_f(0x7FC00000u)), 1u << 9, "fclass qNaN");

    printf("--- fclass.d ---\n");
    check_u32(fclass_d(bits_d(0xFFF0000000000000ULL)), 1u << 0, "fclass.d -inf");
    check_u32(fclass_d(-3.0),                            1u << 1, "fclass.d -norm");
    check_u32(fclass_d(bits_d(0x8000000000000001ULL)), 1u << 2, "fclass.d -sub");
    check_u32(fclass_d(-0.0),                            1u << 3, "fclass.d -0");
    check_u32(fclass_d(0.0),                             1u << 4, "fclass.d +0");
    check_u32(fclass_d(bits_d(0x0000000000000001ULL)), 1u << 5, "fclass.d +sub");
    check_u32(fclass_d(3.0),                             1u << 6, "fclass.d +norm");
    check_u32(fclass_d(bits_d(0x7FF0000000000000ULL)), 1u << 7, "fclass.d +inf");
    check_u32(fclass_d(bits_d(0x7FF0000000000001ULL)), 1u << 8, "fclass.d sNaN");
    check_u32(fclass_d(bits_d(0x7FF8000000000000ULL)), 1u << 9, "fclass.d qNaN");

    printf("--- fcvt.w.s (signed, rtz) ---\n");
    check_u32((uint32_t)fcvt_w_s(0.0f), 0, "w.s 0");
    check_u32((uint32_t)fcvt_w_s(1.9f), 1, "w.s 1.9 rtz");
    check_u32((uint32_t)fcvt_w_s(-1.9f), (uint32_t)-1, "w.s -1.9 rtz");
    check_u32((uint32_t)fcvt_w_s(bits_f(0x7FC00000u)), 0x7FFFFFFFu, "w.s qNaN→MAX");
    check_u32((uint32_t)fcvt_w_s(bits_f(0x7F800000u)), 0x7FFFFFFFu, "w.s +inf→MAX");
    check_u32((uint32_t)fcvt_w_s(bits_f(0xFF800000u)), 0x80000000u, "w.s -inf→MIN");
    check_u32((uint32_t)fcvt_w_s(3.0e9f), 0x7FFFFFFFu, "w.s overflow+");
    check_u32((uint32_t)fcvt_w_s(-3.0e9f), 0x80000000u, "w.s overflow-");

    printf("--- fcvt.wu.s (unsigned, rtz) ---\n");
    check_u32(fcvt_wu_s(0.0f), 0, "wu.s 0");
    check_u32(fcvt_wu_s(1.9f), 1, "wu.s 1.9");
    check_u32(fcvt_wu_s(-1.0f), 0, "wu.s neg→0");
    check_u32(fcvt_wu_s(bits_f(0x7FC00000u)), 0, "wu.s NaN→0");
    check_u32(fcvt_wu_s(bits_f(0x7F800000u)), 0xFFFFFFFFu, "wu.s +inf→MAX");
    check_u32(fcvt_wu_s(5.0e9f), 0xFFFFFFFFu, "wu.s overflow");
    /* 2^32 - 1 is not exact in f32; 4294967040.0f is the largest f32 < 2^32 */
    check_u32(fcvt_wu_s(4294967040.0f), 4294967040u, "wu.s near 2^32");

    printf("--- fcvt.w.d / fcvt.wu.d ---\n");
    check_u32((uint32_t)fcvt_w_d(NAN), 0x7FFFFFFFu, "w.d NaN→MAX");
    check_u32((uint32_t)fcvt_w_d(1e20), 0x7FFFFFFFu, "w.d overflow+");
    check_u32((uint32_t)fcvt_w_d(-1e20), 0x80000000u, "w.d overflow-");
    check_u32((uint32_t)fcvt_w_d(-2.7), (uint32_t)-2, "w.d -2.7 rtz");
    check_u32(fcvt_wu_d(-5.0), 0, "wu.d neg→0");
    check_u32(fcvt_wu_d(NAN), 0, "wu.d NaN→0");
    check_u32(fcvt_wu_d(1e20), 0xFFFFFFFFu, "wu.d overflow");
    check_u32(fcvt_wu_d(4000000000.0), 4000000000u, "wu.d 4e9");

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
