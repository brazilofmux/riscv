/* printf/snprintf format tests */
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

    /* %p — compare against known hex, not pointer value */
    snprintf(buf, sizeof(buf), "%p", (void *)0x12345678);
    check(strstr(buf, "12345678") != NULL, "snprintf %%p");

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
