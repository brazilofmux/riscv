#ifndef __MATH_H__
#define __MATH_H__

/* RV32IM Software Math Library */

#define M_E        2.7182818284590452354
#define M_LOG2E    1.4426950408889634074
#define M_LOG10E   0.43429448190325182765
#define M_LN2      0.69314718055994530942
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.78539816339744830962
#define M_1_PI     0.31830988618379067154
#define M_2_PI     0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2    1.41421356237309504880
#define M_SQRT1_2  0.70710678118654752440

#define HUGE_VAL   (__builtin_huge_val())
#define HUGE_VALF  (__builtin_huge_valf())
#define INFINITY   (__builtin_inff())
#define NAN        (__builtin_nanf(""))

double fabs(double x);
float fabsf(float x);

double sqrt(double x);
float sqrtf(float x);

double pow(double x, double y);
float powf(float x, float y);

double exp(double x);
float expf(float x);

double log(double x);
float logf(float x);

double log10(double x);
float log10f(float x);

double log2(double x);
float log2f(float x);

double sin(double x);
float sinf(float x);

double cos(double x);
float cosf(float x);

double tan(double x);
float tanf(float x);

double asin(double x);
float asinf(float x);

double acos(double x);
float acosf(float x);

double atan(double x);
float atanf(float x);

double atan2(double y, double x);
float atan2f(float y, float x);

double sinh(double x);
float sinhf(float x);

double cosh(double x);
float coshf(float x);

double tanh(double x);
float tanhf(float x);

double ceil(double x);
float ceilf(float x);

double floor(double x);
float floorf(float x);

double round(double x);
float roundf(float x);

double trunc(double x);
float truncf(float x);

double fmod(double x, double y);
float fmodf(float x, float y);

double copysign(double x, double y);
float copysignf(float x, float y);

double ldexp(double x, int exp);
float ldexpf(float x, int exp);

double frexp(double x, int *exp);
float frexpf(float x, int *exp);

double modf(double x, double *iptr);
float modff(float x, float *iptr);

int isnan(double x);
int isinf(double x);
int isfinite(double x);

#endif /* __MATH_H__ */
