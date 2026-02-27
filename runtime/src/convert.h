#ifndef CONVERT_H
#define CONVERT_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Integer to string conversion functions */
/* 32-bit */
size_t slow32_ltoa(int val, char *buf);
size_t slow32_utoa(unsigned int uval, char *buf);
size_t slow32_utoo(unsigned int uval, char *buf);
size_t slow32_utox(unsigned int uval, char *buf, bool uppercase);

/* 64-bit */
size_t slow32_ltoa64(int64_t val, char *buf);
size_t slow32_utoa64(uint64_t uval, char *buf);
size_t slow32_utoo64(uint64_t uval, char *buf);
size_t slow32_utox64(uint64_t uval, char *buf, bool uppercase);

/* Hex digit tables */
extern const char Digits16U[17];
extern const char Digits16L[17];

#endif
