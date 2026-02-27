#ifndef __MATH_H__
#define __MATH_H__

/* Minimal math.h for RV32IM runtime — soft-float stubs */

#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inf())
#define NAN      (__builtin_nan(""))

#define isinf(x)  __builtin_isinf(x)
#define isnan(x)  __builtin_isnan(x)
#define isfinite(x) __builtin_isfinite(x)

#define fabs(x)  __builtin_fabs(x)
#define floor(x) __builtin_floor(x)
#define ceil(x)  __builtin_ceil(x)

#endif /* __MATH_H__ */
