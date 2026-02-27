#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int abs(int n) {
    return (n < 0) ? -n : n;
}

long labs(long n) {
    return (n < 0) ? -n : n;
}

long long llabs(long long n) {
    return (n < 0) ? -n : n;
}

int atoi(const char *nptr) {
    int result = 0;
    int sign = 1;

    while (*nptr == ' ' || (*nptr >= '\t' && *nptr <= '\r')) nptr++;

    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }

    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }

    return result * sign;
}

long atol(const char *nptr) {
    long result = 0;
    int sign = 1;

    while (*nptr == ' ' || (*nptr >= '\t' && *nptr <= '\r')) nptr++;

    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }

    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }

    return result * sign;
}

static unsigned int rand_seed = 1;

int rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed / 65536) & RAND_MAX;
}

void srand(unsigned int seed) {
    rand_seed = seed;
}

/* dtoa_strtod is provided by Gay's dtoa.c */
extern double dtoa_strtod(const char *s00, char **se);

double strtod(const char *nptr, char **endptr) {
    return dtoa_strtod(nptr, endptr);
}

double atof(const char *nptr) {
    return strtod(nptr, (char **)0);
}

char *getenv(const char *name) {
    (void)name;
    return NULL;
}

void exit(int status) {
    fflush(stdout);
    fflush(stderr);
    _exit(status);
}

void abort(void) {
    _exit(127);
}
