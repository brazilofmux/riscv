/* Test file I/O through ECALL-based stdio */
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *filename = "/tmp/rv32_test_fileio.txt";
    const char *test_data = "Hello from RV32IM!\nSecond line.\n";

    /* Write to file */
    FILE *f = fopen(filename, "w");
    if (!f) {
        printf("FAIL: fopen for write\n");
        return 1;
    }
    size_t written = fwrite(test_data, 1, strlen(test_data), f);
    fclose(f);
    printf("Wrote %u bytes\n", (unsigned)written);

    /* Read back */
    f = fopen(filename, "r");
    if (!f) {
        printf("FAIL: fopen for read\n");
        return 1;
    }

    char buf[128];
    size_t total = fread(buf, 1, sizeof(buf) - 1, f);
    buf[total] = '\0';
    fclose(f);
    printf("Read %u bytes\n", (unsigned)total);

    if (strcmp(buf, test_data) == 0) {
        printf("PASS: file round-trip\n");
    } else {
        printf("FAIL: data mismatch\n");
        printf("  expected: %s", test_data);
        printf("  got:      %s", buf);
        return 1;
    }

    /* Test fgets */
    f = fopen(filename, "r");
    char line[64];
    if (fgets(line, sizeof(line), f)) {
        if (strcmp(line, "Hello from RV32IM!\n") == 0) {
            printf("PASS: fgets line 1\n");
        } else {
            printf("FAIL: fgets line 1: got '%s'\n", line);
            fclose(f);
            return 1;
        }
    }
    if (fgets(line, sizeof(line), f)) {
        if (strcmp(line, "Second line.\n") == 0) {
            printf("PASS: fgets line 2\n");
        } else {
            printf("FAIL: fgets line 2: got '%s'\n", line);
            fclose(f);
            return 1;
        }
    }
    fclose(f);

    printf("\nAll file I/O tests passed.\n");
    return 0;
}
