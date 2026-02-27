/* File I/O tests */
#include <stdio.h>
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

int main(void) {
    printf("--- fileio tests ---\n");

    const char *filename = "/tmp/rv32_test_fileio.txt";
    const char *test_data = "Hello from RV32IM!\nSecond line.\n";

    /* Write to file */
    FILE *f = fopen(filename, "w");
    check(f != NULL, "fopen write");
    if (!f) return 1;

    size_t written = fwrite(test_data, 1, strlen(test_data), f);
    check(written == strlen(test_data), "fwrite all bytes");
    fclose(f);

    /* Read back */
    f = fopen(filename, "r");
    check(f != NULL, "fopen read");
    if (!f) return 1;

    char buf[128];
    size_t total = fread(buf, 1, sizeof(buf) - 1, f);
    buf[total] = '\0';
    fclose(f);

    check(strcmp(buf, test_data) == 0, "file round-trip");

    /* Test fgets */
    f = fopen(filename, "r");
    check(f != NULL, "fopen for fgets");
    if (!f) return 1;

    char line[64];
    if (fgets(line, sizeof(line), f)) {
        check(strcmp(line, "Hello from RV32IM!\n") == 0, "fgets line 1");
    } else {
        check(0, "fgets line 1");
    }
    if (fgets(line, sizeof(line), f)) {
        check(strcmp(line, "Second line.\n") == 0, "fgets line 2");
    } else {
        check(0, "fgets line 2");
    }
    fclose(f);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
