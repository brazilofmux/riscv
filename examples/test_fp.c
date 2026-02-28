/* Floating-point extension test for RV32IMFD */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* Force FMA/FMIN/FMAX via builtins to avoid needing library declarations */
#define do_fma(a,b,c)   __builtin_fma((a),(b),(c))
#define do_fmaf(a,b,c)  __builtin_fmaf((a),(b),(c))
#define do_fmin(a,b)     __builtin_fmin((a),(b))
#define do_fmax(a,b)     __builtin_fmax((a),(b))
#define do_copysign(a,b) __builtin_copysign((a),(b))

static int tests_passed = 0;
static int tests_failed = 0;

static void check(int cond, const char *name) {
    if (cond) {
        tests_passed++;
    } else {
        printf("FAIL: %s\n", name);
        tests_failed++;
    }
}

/* Volatile to prevent constant-folding */
static volatile double vd1 = 3.14159265358979323846;
static volatile double vd2 = 2.71828182845904523536;
static volatile float  vf1 = 3.14159265f;
static volatile float  vf2 = 2.71828183f;

static void test_double_arith(void) {
    printf("--- double arithmetic ---\n");
    double a = vd1, b = vd2;
    double sum = a + b;
    double diff = a - b;
    double prod = a * b;
    double quot = a / b;

    check(sum > 5.859 && sum < 5.860, "fadd.d");
    check(diff > 0.423 && diff < 0.424, "fsub.d");
    check(prod > 8.539 && prod < 8.540, "fmul.d");
    check(quot > 1.155 && quot < 1.156, "fdiv.d");

    double sq = sqrt(2.0);
    check(sq > 1.41421 && sq < 1.41422, "fsqrt.d");

    double neg = -a;
    check(neg < -3.14 && neg > -3.15, "fneg.d (fsgnjn)");
    double abs_neg = fabs(neg);
    check(abs_neg > 3.14 && abs_neg < 3.15, "fabs.d (fsgnjx)");
}

static void test_float_arith(void) {
    printf("--- float arithmetic ---\n");
    float a = vf1, b = vf2;
    float sum = a + b;
    float diff = a - b;
    float prod = a * b;
    float quot = a / b;

    check(sum > 5.85f && sum < 5.86f, "fadd.s");
    check(diff > 0.42f && diff < 0.43f, "fsub.s");
    check(prod > 8.53f && prod < 8.55f, "fmul.s");
    check(quot > 1.15f && quot < 1.16f, "fdiv.s");

    float sq = sqrtf(2.0f);
    check(sq > 1.414f && sq < 1.415f, "fsqrt.s");
}

static void test_double_compare(void) {
    printf("--- double compare ---\n");
    double a = vd1, b = vd2;
    check(a == a, "feq.d (equal)");
    check(!(a == b), "feq.d (not equal)");
    check(b < a, "flt.d (less)");
    check(!(a < b), "flt.d (not less)");
    check(b <= a, "fle.d (le)");
    check(a <= a, "fle.d (eq)");
    check(!(a <= b), "fle.d (gt)");

    /* NaN comparisons */
    double nan = 0.0 / 0.0;
    check(!(nan == nan), "feq.d NaN");
    check(!(nan < 1.0), "flt.d NaN");
    check(!(nan <= 1.0), "fle.d NaN");
}

static void test_float_compare(void) {
    printf("--- float compare ---\n");
    float a = vf1, b = vf2;
    check(a == a, "feq.s (equal)");
    check(!(a == b), "feq.s (not equal)");
    check(b < a, "flt.s (less)");
    check(b <= a, "fle.s (le)");
}

static void test_conversions(void) {
    printf("--- conversions ---\n");

    /* int → double → int */
    int i1 = 42;
    double d1 = (double)i1;
    check(d1 == 42.0, "fcvt.d.w (42)");
    int i2 = (int)d1;
    check(i2 == 42, "fcvt.w.d (42)");

    /* negative */
    int i3 = -100;
    double d3 = (double)i3;
    check(d3 == -100.0, "fcvt.d.w (-100)");
    int i4 = (int)d3;
    check(i4 == -100, "fcvt.w.d (-100)");

    /* unsigned */
    unsigned u1 = 3000000000u;
    double d5 = (double)u1;
    check(d5 > 2999999999.0 && d5 < 3000000001.0, "fcvt.d.wu");
    unsigned u2 = (unsigned)d5;
    check(u2 == 3000000000u, "fcvt.wu.d");

    /* float ↔ double */
    float f1 = 1.5f;
    double d6 = (double)f1;
    check(d6 == 1.5, "fcvt.d.s");
    float f2 = (float)d6;
    check(f2 == 1.5f, "fcvt.s.d");

    /* int → float → int */
    int i5 = 1000;
    float f3 = (float)i5;
    check(f3 == 1000.0f, "fcvt.s.w (1000)");
    int i6 = (int)f3;
    check(i6 == 1000, "fcvt.w.s (1000)");

    /* Truncation toward zero */
    double d7 = 3.9;
    check((int)d7 == 3, "fcvt.w.d truncate positive");
    double d8 = -3.9;
    check((int)d8 == -3, "fcvt.w.d truncate negative");
}

static void test_minmax(void) {
    printf("--- min/max ---\n");
    double a = 1.0, b = 2.0;
    check(do_fmin(a, b) == 1.0, "fmin.d");
    check(do_fmax(a, b) == 2.0, "fmax.d");

    /* Check signed zero behavior: fmin(-0, +0) = -0 */
    double pz = 0.0, nz = -0.0;
    double mn = do_fmin(pz, nz);
    uint64_t mn_bits;
    memcpy(&mn_bits, &mn, 8);
    check((mn_bits >> 63) == 1, "fmin.d(-0,+0)=-0");
}

/* Test calling convention: doubles passed in fa0-fa7 */
static double add_doubles(double a, double b, double c, double d) {
    return a + b + c + d;
}

static float add_floats(float a, float b, float c, float d) {
    return a + b + c + d;
}

static void test_calling_convention(void) {
    printf("--- calling convention ---\n");
    double r1 = add_doubles(1.0, 2.0, 3.0, 4.0);
    check(r1 == 10.0, "double args/return");

    float r2 = add_floats(1.0f, 2.0f, 3.0f, 4.0f);
    check(r2 == 10.0f, "float args/return");

    /* Mixed int/float */
    char buf[64];
    sprintf(buf, "%.2f", 3.14);
    check(strcmp(buf, "3.14") == 0, "printf double");
}

static void test_fma(void) {
    printf("--- fused multiply-add ---\n");
    double a = 2.0, b = 3.0, c = 4.0;

    /* fma(a, b, c) = a*b + c = 10 */
    double r1 = do_fma(a, b, c);
    check(r1 == 10.0, "fma.d (a*b+c)");

    /* fma(a, b, -c) = a*b - c = 2 */
    double r2 = do_fma(a, b, -c);
    check(r2 == 2.0, "fma.d (a*b-c)");

    float af = 2.0f, bf = 3.0f, cf = 4.0f;
    float r3 = do_fmaf(af, bf, cf);
    check(r3 == 10.0f, "fma.s");
}

static void test_sign_inject(void) {
    printf("--- sign injection ---\n");

    /* fabs = fsgnjx with same register */
    double neg = -5.0;
    double pos = fabs(neg);
    check(pos == 5.0, "fabs.d");

    /* fneg = fsgnjn with same register */
    double p = 5.0;
    double n = -p;  /* compiler uses fsgnjn or fneg */
    check(n == -5.0, "fneg.d");

    /* copysign */
    double a = 3.0, b = -7.0;
    double r = do_copysign(a, b);
    check(r == -3.0, "copysign (fsgnj.d)");
}

int main(void) {
    printf("=== RV32IMFD Floating-Point Test Suite ===\n\n");

    test_double_arith();
    test_float_arith();
    test_double_compare();
    test_float_compare();
    test_conversions();
    test_minmax();
    test_calling_convention();
    test_fma();
    test_sign_inject();

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
