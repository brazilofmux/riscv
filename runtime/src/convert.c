#include "convert.h"
#include <stdint.h>

static const char Digits100[201] =
    "00010203040506070809"
    "10111213141516171819"
    "20212223242526272829"
    "30313233343536373839"
    "40414243444546474849"
    "50515253545556575859"
    "60616263646566676869"
    "70717273747576777879"
    "80818283848586878889"
    "90919293949596979899";

const char Digits16L[17] = "0123456789abcdef";
const char Digits16U[17] = "0123456789ABCDEF";

static void reverse_string(char *start, char *end) {
    while (start < end) {
        char tmp = *start;
        *start = *end;
        *end = tmp;
        start++;
        end--;
    }
}

size_t slow32_utoa(unsigned int val, char *buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    char *p = buf;
    while (val >= 100) {
        unsigned int rem = val % 100;
        val /= 100;
        const char *digits = Digits100 + (rem * 2);
        *p++ = digits[1];
        *p++ = digits[0];
    }

    if (val >= 10) {
        const char *digits = Digits100 + (val * 2);
        *p++ = digits[1];
        *p++ = digits[0];
    } else {
        *p++ = (char)('0' + val);
    }

    size_t len = (size_t)(p - buf);
    reverse_string(buf, buf + len - 1);
    buf[len] = '\0';
    return len;
}

size_t slow32_ltoa(int val, char *buf) {
    char *p = buf;
    unsigned int uval;

    if (val < 0) {
        *p++ = '-';
        uval = (val == (int)0x80000000) ? 0x80000000U : (unsigned int)(-val);
    } else {
        uval = (unsigned int)val;
    }

    size_t len = slow32_utoa(uval, p);
    return (size_t)(p - buf) + len;
}

#define S32_MAX_DEC_DIGITS 20

size_t slow32_utoa64(uint64_t val, char *buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    char scratch[S32_MAX_DEC_DIGITS];
    size_t idx = 0;

    while (val >= 100) {
        uint64_t rem = val % 100ULL;
        val = val / 100ULL;
        const char *digits = Digits100 + (rem * 2);
        scratch[idx++] = digits[1];
        scratch[idx++] = digits[0];
    }

    if (val >= 10) {
        const char *digits = Digits100 + (val * 2);
        scratch[idx++] = digits[1];
        scratch[idx++] = digits[0];
    } else {
        scratch[idx++] = (char)('0' + val);
    }

    size_t len = idx;
    char *out = buf;
    while (idx > 0) {
        *out++ = scratch[--idx];
    }
    *out = '\0';
    return len;
}

size_t slow32_ltoa64(int64_t val, char *buf) {
    char *p = buf;
    uint64_t uval;

    if (val < 0) {
        *p++ = '-';
        uval = (val == (int64_t)0x8000000000000000LL) ? 0x8000000000000000ULL : (uint64_t)(-val);
    } else {
        uval = (uint64_t)val;
    }

    p += slow32_utoa64(uval, p);
    return p - buf;
}

size_t slow32_utox(unsigned int val, char *buf, bool uppercase) {
    char *p = buf;
    char *q = p;
    const char *digits = uppercase ? Digits16U : Digits16L;

    if (val == 0) {
        *p++ = '0';
        *p = '\0';
        return 1;
    }

    while (val > 0) {
        *p++ = digits[val & 0xF];
        val >>= 4;
    }

    *p = '\0';
    reverse_string(q, p - 1);
    return p - buf;
}

size_t slow32_utox64(uint64_t val, char *buf, bool uppercase) {
    char *p = buf;
    char *q = p;
    const char *digits = uppercase ? Digits16U : Digits16L;

    if (val == 0) {
        *p++ = '0';
        *p = '\0';
        return 1;
    }

    while (val > 0) {
        *p++ = digits[val & 0xF];
        val >>= 4;
    }

    *p = '\0';
    reverse_string(q, p - 1);
    return p - buf;
}

size_t slow32_utoo(unsigned int val, char *buf) {
    char *p = buf;
    char *q = p;

    if (val == 0) {
        *p++ = '0';
        *p = '\0';
        return 1;
    }

    while (val > 0) {
        *p++ = '0' + (val & 7);
        val >>= 3;
    }

    *p = '\0';
    reverse_string(q, p - 1);
    return p - buf;
}

size_t slow32_utoo64(uint64_t val, char *buf) {
    char *p = buf;
    char *q = p;

    if (val == 0) {
        *p++ = '0';
        *p = '\0';
        return 1;
    }

    while (val > 0) {
        *p++ = '0' + (val & 7ULL);
        val >>= 3;
    }

    *p = '\0';
    reverse_string(q, p - 1);
    return p - buf;
}
