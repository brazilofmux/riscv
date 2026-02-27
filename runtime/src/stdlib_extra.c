#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* Quicksort with median-of-three pivot + insertion sort for small partitions */

#define QSORT_INSERTION_THRESHOLD 16

static void insertion_sort_r(char *base, size_t num, size_t size,
                             int (*compar)(const void *, const void *, void *),
                             void *arg) {
    for (size_t i = 1; i < num; i++) {
        size_t j = i;
        while (j > 0 && compar(base + (j - 1) * size, base + j * size, arg) > 0) {
            memswap(base + (j - 1) * size, base + j * size, size);
            j--;
        }
    }
}

static void median_of_three(char *base, size_t num, size_t size,
                             int (*compar)(const void *, const void *, void *),
                             void *arg) {
    char *a = base;
    char *b = base + (num / 2) * size;
    char *c = base + (num - 1) * size;
    if (compar(a, b, arg) > 0) memswap(a, b, size);
    if (compar(b, c, arg) > 0) memswap(b, c, size);
    if (compar(a, b, arg) > 0) memswap(a, b, size);
    memswap(b, c, size);
}

void qsort_r(void *base, size_t nmemb, size_t size,
             int (*compar)(const void *, const void *, void *),
             void *arg) {
    char *lo = (char *)base;
    size_t num = nmemb;

    if (base == NULL || nmemb == 0 || size == 0 || compar == NULL)
        return;

    for (;;) {
        if (num <= 1) return;
        if (num <= QSORT_INSERTION_THRESHOLD) {
            insertion_sort_r(lo, num, size, compar, arg);
            return;
        }

        median_of_three(lo, num, size, compar, arg);

        {
            char *pivot = lo + (num - 1) * size;
            size_t i = 0;
            size_t j;
            size_t left_size, right_size;

            for (j = 0; j < num - 1; j++) {
                if (compar(lo + j * size, pivot, arg) <= 0) {
                    memswap(lo + i * size, lo + j * size, size);
                    i++;
                }
            }
            memswap(lo + i * size, pivot, size);

            left_size = i;
            right_size = num - i - 1;

            if (left_size <= right_size) {
                qsort_r(lo, left_size, size, compar, arg);
                lo = lo + (i + 1) * size;
                num = right_size;
            } else {
                qsort_r(lo + (i + 1) * size, right_size, size, compar, arg);
                num = left_size;
            }
        }
    }
}

static int qsort_compat_compar(const void *a, const void *b, void *arg) {
    int (*compar)(const void *, const void *) = (int (*)(const void *, const void *))arg;
    return compar(a, b);
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    qsort_r(base, nmemb, size, qsort_compat_compar, (void *)compar);
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *)) {
    if (base == NULL || nmemb == 0 || size == 0 || compar == NULL) {
        return NULL;
    }

    const char *array = (const char *)base;
    size_t left = 0;
    size_t right = nmemb;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const void *mid_elem = array + mid * size;
        int cmp = compar(key, mid_elem);

        if (cmp < 0) {
            right = mid;
        } else if (cmp > 0) {
            left = mid + 1;
        } else {
            return (void *)mid_elem;
        }
    }

    return NULL;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    long result = 0;
    int sign = 1;
    int found_digit = 0;

    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;

    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                base = 16;
                s++;
            } else {
                base = 8;
                found_digit = 1;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
            s += 2;
        }
    }

    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            digit = *s - 'A' + 10;
        } else {
            break;
        }

        if (digit >= base) break;

        found_digit = 1;
        result = result * base + digit;
        s++;
    }

    if (endptr) {
        *endptr = found_digit ? (char *)s : (char *)nptr;
    }

    return result * sign;
}

div_t div(int numer, int denom) {
    div_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

ldiv_t ldiv(long numer, long denom) {
    ldiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}
