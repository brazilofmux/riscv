/* Comprehensive libc test for RV32IM runtime */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(int cond, const char *name) {
    if (cond) {
        tests_passed++;
    } else {
        printf("FAIL: %s\n", name);
        tests_failed++;
    }
}

static void test_string(void) {
    printf("--- string tests ---\n");

    check(strlen("hello") == 5, "strlen");
    check(strlen("") == 0, "strlen empty");

    char buf[64];
    strcpy(buf, "hello");
    check(strcmp(buf, "hello") == 0, "strcpy+strcmp");

    strncpy(buf, "world", 3);
    check(buf[0] == 'w' && buf[1] == 'o' && buf[2] == 'r', "strncpy");

    strcpy(buf, "abc");
    strcat(buf, "def");
    check(strcmp(buf, "abcdef") == 0, "strcat");

    check(strcmp("abc", "abc") == 0, "strcmp eq");
    check(strcmp("abc", "abd") < 0, "strcmp lt");
    check(strcmp("abd", "abc") > 0, "strcmp gt");

    memset(buf, 'x', 5);
    buf[5] = '\0';
    check(strcmp(buf, "xxxxx") == 0, "memset");

    char src[] = "overlap";
    memmove(src + 2, src, 5);
    check(src[2] == 'o' && src[3] == 'v', "memmove overlap");

    check(memcmp("abc", "abc", 3) == 0, "memcmp eq");
    check(memcmp("abc", "abd", 3) < 0, "memcmp lt");

    check(strchr("hello", 'l') != NULL, "strchr found");
    check(strchr("hello", 'z') == NULL, "strchr not found");

    check(strstr("hello world", "world") != NULL, "strstr found");
    check(strstr("hello world", "xyz") == NULL, "strstr not found");
}

static void test_ctype(void) {
    printf("--- ctype tests ---\n");

    check(isalpha('A'), "isalpha A");
    check(isalpha('z'), "isalpha z");
    check(!isalpha('5'), "!isalpha 5");

    check(isdigit('0'), "isdigit 0");
    check(isdigit('9'), "isdigit 9");
    check(!isdigit('a'), "!isdigit a");

    check(isspace(' '), "isspace space");
    check(isspace('\n'), "isspace newline");
    check(!isspace('a'), "!isspace a");

    check(toupper('a') == 'A', "toupper");
    check(tolower('A') == 'a', "tolower");
}

static void test_malloc(void) {
    printf("--- malloc tests ---\n");

    int *p = malloc(sizeof(int) * 10);
    check(p != NULL, "malloc non-null");

    for (int i = 0; i < 10; i++) p[i] = i * 7;
    check(p[0] == 0 && p[5] == 35 && p[9] == 63, "malloc read/write");

    free(p);
    check(1, "free no crash");

    /* calloc */
    int *q = calloc(10, sizeof(int));
    check(q != NULL, "calloc non-null");
    int all_zero = 1;
    for (int i = 0; i < 10; i++) {
        if (q[i] != 0) all_zero = 0;
    }
    check(all_zero, "calloc zeroed");
    free(q);

    /* realloc */
    char *r = malloc(16);
    strcpy(r, "hello");
    r = realloc(r, 64);
    check(strcmp(r, "hello") == 0, "realloc preserves data");
    free(r);
}

static void test_stdlib(void) {
    printf("--- stdlib tests ---\n");

    check(abs(-42) == 42, "abs negative");
    check(abs(42) == 42, "abs positive");

    check(atoi("12345") == 12345, "atoi positive");
    check(atoi("-99") == -99, "atoi negative");
    check(atoi("0") == 0, "atoi zero");

    check(strtol("0xFF", NULL, 16) == 255, "strtol hex");
    check(strtol("-100", NULL, 10) == -100, "strtol negative");
}

static void test_printf(void) {
    printf("--- printf tests ---\n");

    char buf[256];

    snprintf(buf, sizeof(buf), "%d", 42);
    check(strcmp(buf, "42") == 0, "snprintf %%d");

    snprintf(buf, sizeof(buf), "%d", -123);
    check(strcmp(buf, "-123") == 0, "snprintf %%d negative");

    snprintf(buf, sizeof(buf), "%u", 4294967295u);
    check(strcmp(buf, "4294967295") == 0, "snprintf %%u max");

    snprintf(buf, sizeof(buf), "%x", 0xDEAD);
    check(strcmp(buf, "dead") == 0, "snprintf %%x");

    snprintf(buf, sizeof(buf), "%08X", 0xBEEF);
    check(strcmp(buf, "0000BEEF") == 0, "snprintf %%08X");

    snprintf(buf, sizeof(buf), "%s", "hello");
    check(strcmp(buf, "hello") == 0, "snprintf %%s");

    snprintf(buf, sizeof(buf), "%c", 'Z');
    check(strcmp(buf, "Z") == 0, "snprintf %%c");

    snprintf(buf, sizeof(buf), "%10d", 42);
    check(strcmp(buf, "        42") == 0, "snprintf width");

    snprintf(buf, sizeof(buf), "%-10d|", 42);
    check(strcmp(buf, "42        |") == 0, "snprintf left-align");

    snprintf(buf, sizeof(buf), "%p", (void *)0x12345678);
    check(strstr(buf, "12345678") != NULL, "snprintf %%p");
}

int main(void) {
    test_string();
    test_ctype();
    test_malloc();
    test_stdlib();
    test_printf();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);

    return tests_failed;
}
