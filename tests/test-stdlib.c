/* Standard library utility tests */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int int_cmp(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

int main(void) {
    printf("--- stdlib tests ---\n");

    check(abs(-42) == 42, "abs negative");
    check(abs(42) == 42, "abs positive");

    check(atoi("12345") == 12345, "atoi positive");
    check(atoi("-99") == -99, "atoi negative");
    check(atoi("0") == 0, "atoi zero");

    check(strtol("0xFF", NULL, 16) == 255, "strtol hex");
    check(strtol("-100", NULL, 10) == -100, "strtol negative");

    /* qsort */
    int arr[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
    qsort(arr, 10, sizeof(int), int_cmp);
    int sorted = 1;
    for (int i = 0; i < 10; i++) {
        if (arr[i] != i) sorted = 0;
    }
    check(sorted, "qsort");

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
