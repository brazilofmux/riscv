#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "util.h"

void str_upper(char *s) {
    while (*s) {
        if (*s >= 'a' && *s <= 'z')
            *s -= 32;
        s++;
    }
}

int str_icmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int str_imatch(const char *input, const char *prefix) {
    while (*prefix) {
        int ci = *input, cp = *prefix;
        if (ci >= 'a' && ci <= 'z') ci -= 32;
        if (cp >= 'a' && cp <= 'z') cp -= 32;
        if (ci != cp) return 0;
        input++;
        prefix++;
    }
    /* Word boundary: end of string, or whitespace/punctuation after prefix */
    if (*input == '\0' || *input == ' ' || *input == '\t' ||
        *input == ',' || *input == '(' || *input == '"' || *input == '\'')
        return 1;
    return 0;
}

char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return (char *)s;
}

void trim_right(char *s) {
    int len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                       s[len-1] == '\r' || s[len-1] == '\n'))
        len--;
    s[len] = '\0';
}

int read_line(char *buf, int size) {
    int c, i = 0;
    fflush(stdout);
    while (i < size - 1) {
        c = getchar();
        if (c == -1) {  /* EOF */
            if (i == 0) return -1;
            break;
        }
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

void str_copy(char *dst, const char *src, int n) {
    int i;
    for (i = 0; i < n - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

void path_normalize(char *s) {
    while (*s) {
        if (*s == '\\') *s = '/';
        s++;
    }
}

int str_nicmp(const char *a, const char *b, int n) {
    int i;
    for (i = 0; i < n && *a && *b; i++, a++, b++) {
        int ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return ca - cb;
    }
    if (i == n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

int is_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

int is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

/* Case-insensitive wildcard match: '*' matches any sequence, '?' matches any char */
int str_like(const char *text, const char *pattern) {
    const char *t = text;
    const char *p = pattern;
    const char *star = NULL;
    const char *star_text = NULL;

    for (;;) {
        char pc, tc;
        if (*p == '*') {
            while (*p == '*') p++;
            if (*p == '\0') return 1;
            star = p;
            star_text = t;
            continue;
        }

        if (*p == '\0') return *t == '\0';

        if (*t == '\0') {
            /* Remaining pattern must be all '*' to match */
            while (*p == '*') p++;
            return *p == '\0';
        }

        pc = *p;
        tc = *t;
        if (pc >= 'a' && pc <= 'z') pc -= 32;
        if (tc >= 'a' && tc <= 'z') tc -= 32;

        if (pc == '?' || pc == tc) {
            p++;
            t++;
            continue;
        }

        if (star) {
            star_text++;
            t = star_text;
            p = star;
            continue;
        }
        return 0;
    }
}
