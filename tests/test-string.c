/* String function tests */
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

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
