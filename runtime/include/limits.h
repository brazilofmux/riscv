#ifndef _LIMITS_H
#define _LIMITS_H

/* RV32IM: 32-bit machine with native 32-bit integers */

#define CHAR_BIT    8
#define SCHAR_MIN   (-128)
#define SCHAR_MAX   127
#define UCHAR_MAX   255
#define CHAR_MIN    SCHAR_MIN
#define CHAR_MAX    SCHAR_MAX
#define MB_LEN_MAX  1

#define SHRT_MIN    (-32768)
#define SHRT_MAX    32767
#define USHRT_MAX   65535

#define INT_MIN     (-2147483647-1)
#define INT_MAX     2147483647
#define UINT_MAX    4294967295U

#define LONG_MIN    (-2147483647L-1)
#define LONG_MAX    2147483647L
#define ULONG_MAX   4294967295UL

#define LLONG_MIN   (-9223372036854775807LL-1)
#define LLONG_MAX   9223372036854775807LL
#define ULLONG_MAX  18446744073709551615ULL

#define PATH_MAX    4096
#define NAME_MAX    255
#define OPEN_MAX    256

#define WORD_BIT    32
#define LONG_BIT    32
#define SIZE_MAX    UINT_MAX
#define SSIZE_MAX   INT_MAX

#endif
