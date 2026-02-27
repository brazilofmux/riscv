/*
 * RV32IM Software Math Library (libm) — soft implementations
 *
 * Ported from SLOW-32 math_soft.c with additions:
 *   - fabs/fabsf (bit manipulation, no FPU)
 *   - sqrt/sqrtf (Newton's method with IEEE exponent seed)
 *
 * MUST be compiled with -fno-builtin to prevent the compiler from
 * recognizing standard library patterns and emitting recursive calls.
 */

#include <math.h>

/* ================================================================
 * IEEE 754 bit manipulation (little-endian doubles: IEEE_8087)
 * u[0] = low 32 bits, u[1] = high 32 bits (sign | exponent | mantissa_hi)
 * ================================================================ */

typedef union { double d; unsigned int u[2]; } dbl_bits;
typedef union { float f; unsigned int u; } flt_bits;

/* Construct special values without relying on FP exceptions */
static double mk_nan(void)  { dbl_bits b; b.u[0] = 0; b.u[1] = 0x7FF80000; return b.d; }
static double mk_inf(void)  { dbl_bits b; b.u[0] = 0; b.u[1] = 0x7FF00000; return b.d; }
static double mk_ninf(void) { dbl_bits b; b.u[0] = 0; b.u[1] = 0xFFF00000; return b.d; }

/* Internal constants */
#define PI      3.14159265358979323846
#define TWO_PI  6.28318530717958647692
#define HALF_PI 1.57079632679489661923
#define LN2     0.69314718055994530942
#define LOG10E  0.43429448190325182765

/* ================================================================
 * Classification
 * ================================================================ */

int isnan(double x) {
    return x != x;
}

int isinf(double x) {
    dbl_bits b;
    b.d = x;
    return ((b.u[1] & 0x7FF00000) == 0x7FF00000) &&
           ((b.u[1] & 0x000FFFFF) == 0) && (b.u[0] == 0);
}

int isfinite(double x) {
    dbl_bits b;
    b.d = x;
    return (b.u[1] & 0x7FF00000) != 0x7FF00000;
}

/* ================================================================
 * Absolute value — bit manipulation, no FPU needed
 * ================================================================ */

double fabs(double x) {
    dbl_bits b;
    b.d = x;
    b.u[1] &= 0x7FFFFFFF;
    return b.d;
}

float fabsf(float x) {
    flt_bits b;
    b.f = x;
    b.u &= 0x7FFFFFFF;
    return b.f;
}

/* ================================================================
 * Basic
 * ================================================================ */

double copysign(double x, double y) {
    dbl_bits bx, by;
    bx.d = x;
    by.d = y;
    bx.u[1] = (bx.u[1] & 0x7FFFFFFF) | (by.u[1] & 0x80000000);
    return bx.d;
}

/* ================================================================
 * Rounding
 * ================================================================ */

double floor(double x) {
    if (x != x) return x;                                  /* NaN */
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0)
        return x;                                          /* |x| >= 2^52: integer */
    long long i = (long long)x;
    double d = (double)i;
    if (x < d) return d - 1.0;   /* negative non-integer */
    return d;
}

double ceil(double x) {
    return -floor(-x);
}

double trunc(double x) {
    if (x != x) return x;
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0)
        return x;
    return (double)(long long)x;
}

double round(double x) {
    return (x >= 0.0) ? floor(x + 0.5) : ceil(x - 0.5);
}

double modf(double x, double *iptr) {
    double t = trunc(x);
    *iptr = t;
    return x - t;
}

/* ================================================================
 * IEEE 754 decomposition
 * ================================================================ */

double frexp(double x, int *exp) {
    dbl_bits db;
    db.d = x;
    unsigned int hi = db.u[1];
    int e = (hi >> 20) & 0x7FF;

    /* +-0 */
    if (e == 0 && db.u[0] == 0 && (hi & 0x7FFFFFFF) == 0) {
        *exp = 0;
        return x;
    }
    /* inf / NaN */
    if (e == 0x7FF) {
        *exp = 0;
        return x;
    }
    /* Subnormal: scale up by 2^64 to normalize */
    if (e == 0) {
        db.d *= 18446744073709551616.0;  /* 2^64 */
        hi = db.u[1];
        e = (hi >> 20) & 0x7FF;
        *exp = e - 1022 - 64;
    } else {
        *exp = e - 1022;
    }
    /* Set exponent to -1 (biased 1022) giving result in [0.5, 1.0) */
    db.u[1] = (hi & 0x800FFFFF) | (1022 << 20);
    return db.d;
}

double ldexp(double x, int n) {
    if (n == 0 || x == 0.0 || x != x) return x;

    dbl_bits scale;

    /* Scale in steps to avoid exponent overflow */
    if (n > 1023) {
        scale.u[0] = 0; scale.u[1] = 0x7FE00000;  /* 2^1023 */
        x *= scale.d; n -= 1023;
        if (n > 1023) {
            x *= scale.d; n -= 1023;
            if (n > 1023) n = 1023;
        }
    } else if (n < -1022) {
        scale.u[0] = 0; scale.u[1] = 0x00100000;  /* 2^-1022 */
        x *= scale.d; n += 1022;
        if (n < -1022) {
            x *= scale.d; n += 1022;
            if (n < -1022) n = -1022;
        }
    }

    scale.u[0] = 0;
    scale.u[1] = (unsigned int)(n + 1023) << 20;
    return x * scale.d;
}

/* ================================================================
 * fmod — C standard: x - trunc(x/y) * y
 * ================================================================ */

double fmod(double x, double y) {
    if (y == 0.0 || x != x || y != y) return mk_nan();
    if (x == 0.0) return x;
    double q = trunc(x / y);
    double r = x - q * y;
    return r;
}

/* ================================================================
 * Square root — Newton's method with IEEE exponent seed
 * ================================================================ */

double sqrt(double x) {
    if (x < 0.0) return mk_nan();
    if (x == 0.0 || x != x) return x;
    /* Check for +inf */
    {
        dbl_bits b;
        b.d = x;
        if (b.u[1] == 0x7FF00000 && b.u[0] == 0) return x;
    }
    /* Initial guess: halve the exponent */
    dbl_bits b;
    b.d = x;
    b.u[1] = ((b.u[1] - 0x3FF00000) >> 1) + 0x3FF00000;
    double g = b.d;
    /* 6 Newton iterations for full double precision */
    for (int i = 0; i < 6; i++)
        g = 0.5 * (g + x / g);
    return g;
}

float sqrtf(float x) {
    return (float)sqrt((double)x);
}

/* ================================================================
 * Exponential and logarithmic
 * ================================================================ */

/* exp(x) via Taylor series with range reduction
 * exp(x) = 2^k * exp(r) where r = x - k*ln(2), |r| < ln(2)/2 */
double exp(double x) {
    if (x > 709.0) return mk_inf();
    if (x < -709.0) return 0.0;

    int k = (int)(x / LN2 + (x >= 0.0 ? 0.5 : -0.5));
    double r = x - (double)k * LN2;

    /* Taylor series for exp(r), |r| < 0.35 */
    double term = 1.0, sum = 1.0;
    for (int i = 1; i <= 25; i++) {
        term *= r / (double)i;
        sum += term;
        if (fabs(term) < 1e-16 * fabs(sum)) break;
    }
    return ldexp(sum, k);
}

/* log(x) via atanh series after range reduction to [0.5, 2.0] */
double log(double x) {
    if (x < 0.0) return mk_nan();
    if (x == 0.0) return mk_ninf();
    if (x != x) return x;

    double y = 0.0, t = x;
    while (t > 2.0) { t *= 0.5; y += LN2; }
    while (t < 0.5) { t *= 2.0; y -= LN2; }

    /* log(t) = 2 * atanh((t-1)/(t+1)) for t in [0.5, 2.0] */
    double u = (t - 1.0) / (t + 1.0);
    double u2 = u * u;
    double sum = 0.0, term = u;
    for (int i = 0; i < 30; i++) {
        sum += term / (double)(2 * i + 1);
        term *= u2;
    }
    return y + 2.0 * sum;
}

double log10(double x) {
    return log(x) * LOG10E;
}

double log2(double x) {
    return log(x) * (1.0 / LN2);
}

float log2f(float x) {
    return (float)log2((double)x);
}

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 0.0) return 0.0;
    if (x == 1.0) return 1.0;

    /* Integer exponent fast path: binary exponentiation */
    if (y == (double)(int)y && y > 0.0) {
        int n = (int)y;
        double r = 1.0, b = x;
        while (n > 0) {
            if (n & 1) r *= b;
            b *= b;
            n >>= 1;
        }
        return r;
    }
    if (y == (double)(int)y && y < 0.0)
        return 1.0 / pow(x, -y);

    /* General: x^y = exp(y * log(x)) */
    if (x < 0.0) return mk_nan();  /* fractional exponent of negative base */
    return exp(y * log(x));
}

/* ================================================================
 * Trigonometric — Taylor series with range reduction
 * ================================================================ */

double sin(double x) {
    if (x != x) return x;

    /* Range reduce to [-pi, pi] */
    x = fmod(x, TWO_PI);
    if (x > PI) x -= TWO_PI;
    if (x < -PI) x += TWO_PI;

    /* Taylor: sin(x) = x - x^3/3! + x^5/5! - ... */
    double x2 = x * x;
    double term = x, sum = x;
    for (int i = 1; i <= 15; i++) {
        term *= -x2 / (double)(2 * i * (2 * i + 1));
        sum += term;
    }
    return sum;
}

double cos(double x) {
    return sin(x + HALF_PI);
}

double tan(double x) {
    double c = cos(x);
    if (fabs(c) < 1e-15)
        return (sin(x) > 0.0) ? 1e15 : -1e15;
    return sin(x) / c;
}

/* ================================================================
 * Inverse trigonometric
 * ================================================================ */

/* atan(x) via Taylor series with range reduction */
double atan(double x) {
    if (x != x) return x;

    int neg = 0, recip = 0;
    if (x < 0.0) { neg = 1; x = -x; }
    if (x > 1.0) { recip = 1; x = 1.0 / x; }

    double result;
    if (x > 0.2679) {
        /* Reduce further: atan(x) = pi/6 + atan((x*sqrt3-1)/(sqrt3+x)) */
        double sqrt3 = 1.7320508075688772;
        double t = (x * sqrt3 - 1.0) / (sqrt3 + x);
        double t2 = t * t, sum = t, term = t;
        for (int i = 1; i <= 20; i++) {
            term *= -t2;
            sum += term / (double)(2 * i + 1);
        }
        result = 0.5235987755982988 + sum;  /* pi/6 */
    } else {
        double x2 = x * x, sum = x, term = x;
        for (int i = 1; i <= 20; i++) {
            term *= -x2;
            sum += term / (double)(2 * i + 1);
        }
        result = sum;
    }

    if (recip) result = HALF_PI - result;
    if (neg) result = -result;
    return result;
}

double atan2(double y, double x) {
    if (x != x || y != y) return mk_nan();

    if (x > 0.0) return atan(y / x);
    if (x < 0.0)
        return (y >= 0.0) ? atan(y / x) + PI : atan(y / x) - PI;
    /* x == 0 */
    if (y > 0.0) return HALF_PI;
    if (y < 0.0) return -HALF_PI;
    return 0.0;
}

double asin(double x) {
    if (x < -1.0 || x > 1.0) return mk_nan();
    if (x == 1.0) return HALF_PI;
    if (x == -1.0) return -HALF_PI;
    return atan(x / sqrt(1.0 - x * x));
}

double acos(double x) {
    if (x < -1.0 || x > 1.0) return mk_nan();
    return HALF_PI - asin(x);
}

/* ================================================================
 * Hyperbolic
 * ================================================================ */

double sinh(double x) {
    if (fabs(x) < 1e-10) return x;  /* sinh(x) ~ x for small x */
    double ex = exp(x);
    return (ex - 1.0 / ex) * 0.5;
}

double cosh(double x) {
    double ex = exp(fabs(x));
    return (ex + 1.0 / ex) * 0.5;
}

double tanh(double x) {
    if (x > 20.0) return 1.0;
    if (x < -20.0) return -1.0;
    double e2x = exp(2.0 * x);
    return (e2x - 1.0) / (e2x + 1.0);
}

/* ================================================================
 * Float wrappers — cast through double versions
 * ================================================================ */

float sinf(float x)                { return (float)sin((double)x); }
float cosf(float x)                { return (float)cos((double)x); }
float tanf(float x)                { return (float)tan((double)x); }
float asinf(float x)               { return (float)asin((double)x); }
float acosf(float x)               { return (float)acos((double)x); }
float atanf(float x)               { return (float)atan((double)x); }
float sinhf(float x)               { return (float)sinh((double)x); }
float coshf(float x)               { return (float)cosh((double)x); }
float tanhf(float x)               { return (float)tanh((double)x); }
float expf(float x)                { return (float)exp((double)x); }
float logf(float x)                { return (float)log((double)x); }
float log10f(float x)              { return (float)log10((double)x); }
float ceilf(float x)               { return (float)ceil((double)x); }
float floorf(float x)              { return (float)floor((double)x); }
float roundf(float x)              { return (float)round((double)x); }
float truncf(float x)              { return (float)trunc((double)x); }
float fmodf(float x, float y)      { return (float)fmod((double)x, (double)y); }
float powf(float x, float y)       { return (float)pow((double)x, (double)y); }
float atan2f(float y, float x)     { return (float)atan2((double)y, (double)x); }
float ldexpf(float x, int n)       { return (float)ldexp((double)x, n); }

float copysignf(float x, float y) {
    flt_bits bx, by;
    bx.f = x; by.f = y;
    bx.u = (bx.u & 0x7FFFFFFF) | (by.u & 0x80000000);
    return bx.f;
}

float frexpf(float x, int *exp) {
    return (float)frexp((double)x, exp);
}

float modff(float x, float *iptr) {
    double di;
    double r = modf((double)x, &di);
    *iptr = (float)di;
    return (float)r;
}
