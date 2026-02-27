/* Character classification tests */
#include <stdio.h>
#include <ctype.h>

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

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
