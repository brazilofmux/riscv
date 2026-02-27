/* Dynamic memory allocation tests */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("PASS: %s\n", name);
        tests_passed++;
    } else {
        printf("FAIL: %s\n", name);
        tests_failed++;
    }
}

int main(void) {
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

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
