/* inttypes.h — printf/scanf format macros for fixed-width integer types.
 *
 * Borrowed from ~/slow-32/selfhost/stage07/include/inttypes.h. RV32IMFD's
 * ABI is ILP32 with `long long` 64-bit, so the 64-bit macros use `ll`. */
#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

#define PRId8   "d"
#define PRIi8   "i"
#define PRIo8   "o"
#define PRIu8   "u"
#define PRIx8   "x"
#define PRIX8   "X"

#define PRId16  "d"
#define PRIi16  "i"
#define PRIo16  "o"
#define PRIu16  "u"
#define PRIx16  "x"
#define PRIX16  "X"

#define PRId32  "d"
#define PRIi32  "i"
#define PRIo32  "o"
#define PRIu32  "u"
#define PRIx32  "x"
#define PRIX32  "X"

#define PRId64  "lld"
#define PRIi64  "lli"
#define PRIo64  "llo"
#define PRIu64  "llu"
#define PRIx64  "llx"
#define PRIX64  "llX"

/* Pointer-sized integers: ILP32, so 32-bit. */
#define PRIdPTR "d"
#define PRIuPTR "u"
#define PRIxPTR "x"

#define SCNd8   "hhd"
#define SCNi8   "hhi"
#define SCNu8   "hhu"
#define SCNx8   "hhx"

#define SCNd16  "hd"
#define SCNi16  "hi"
#define SCNu16  "hu"
#define SCNx16  "hx"

#define SCNd32  "d"
#define SCNi32  "i"
#define SCNu32  "u"
#define SCNx32  "x"

#define SCNd64  "lld"
#define SCNi64  "lli"
#define SCNu64  "llu"
#define SCNx64  "llx"

#endif /* _INTTYPES_H */
