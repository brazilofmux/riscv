/* setjmp/longjmp tests */
#include <stdio.h>
#include <setjmp.h>

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

/* Test basic setjmp/longjmp */
static jmp_buf buf1;

static void do_longjmp(void) {
    longjmp(buf1, 42);
}

/* Test longjmp with value 0 returns 1 */
static jmp_buf buf2;

/* Test nested function calls */
static jmp_buf buf3;
static int depth_reached = 0;

static void nested_c(void) {
    depth_reached = 3;
    longjmp(buf3, 99);
}

static void nested_b(void) {
    depth_reached = 2;
    nested_c();
}

static void nested_a(void) {
    depth_reached = 1;
    nested_b();
}

int main(void) {
    printf("--- setjmp tests ---\n");

    /* Test 1: setjmp returns 0 on first call */
    {
        int val = setjmp(buf1);
        if (val == 0) {
            check(1, "setjmp returns 0");
            /* Test 2: longjmp returns the value */
            do_longjmp();
            check(0, "should not reach here");
        } else {
            check(val == 42, "longjmp value 42");
        }
    }

    /* Test 3: longjmp(env, 0) returns 1 */
    {
        int val = setjmp(buf2);
        if (val == 0) {
            longjmp(buf2, 0);
            check(0, "should not reach here");
        } else {
            check(val == 1, "longjmp(env,0) returns 1");
        }
    }

    /* Test 4: longjmp across nested function calls */
    {
        int val = setjmp(buf3);
        if (val == 0) {
            nested_a();
            check(0, "should not reach here");
        } else {
            check(val == 99, "nested longjmp value");
            check(depth_reached == 3, "nested depth reached");
        }
    }

    /* Test 5: multiple setjmp/longjmp cycles */
    {
        static jmp_buf buf4;
        volatile int count = 0;
        int val = setjmp(buf4);
        count++;
        if (val < 3) {
            longjmp(buf4, val + 1);
        }
        check(val == 3, "multiple longjmp cycles value");
        check(count == 4, "multiple longjmp cycles count");
    }

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
