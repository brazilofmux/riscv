/*
 * Test: large struct copy (260 bytes) — reproduces the DBT bug where
 * -O2 struct copies are truncated to ~16 bytes.
 *
 * Matches dBASE's value_t layout: 4-byte type + 256-byte union.
 */
#include <stdio.h>
#include <string.h>

typedef enum { VAL_NIL, VAL_NUM, VAL_CHAR, VAL_DATE, VAL_LOGIC } val_type_t;

typedef struct {
    val_type_t type;
    union {
        char str[256];
        double num;
        int    logic;
    };
} value_t;

static int failures = 0;

static void check(const char *test, int ok) {
    if (!ok) {
        printf("FAIL: %s\n", test);
        failures++;
    }
}

/* Return a string value by value — this is the pattern dBASE uses (val_str) */
static value_t make_str(const char *s) {
    value_t v;
    v.type = VAL_CHAR;
    memset(v.str, 0, sizeof(v.str));
    /* Fill with a distinctive pattern so we can detect truncation */
    int len = strlen(s);
    if (len > 255) len = 255;
    memcpy(v.str, s, len);
    return v;
}

/* Return a numeric value by value */
static value_t make_num(double n) {
    value_t v;
    v.type = VAL_NUM;
    memset(&v, 0, sizeof(v));
    v.type = VAL_NUM;
    v.num = n;
    return v;
}

/* Test 1: Return a short string by value and verify */
static void test_short_string(void) {
    value_t v = make_str("Hello");
    check("short_string type", v.type == VAL_CHAR);
    check("short_string content", strcmp(v.str, "Hello") == 0);
}

/* Test 2: Return a string that fills the whole 256-byte buffer.
 * This is the key test — the pattern fills all 256 bytes with
 * known values that we verify byte-by-byte. */
static void test_long_string(void) {
    char buf[256];
    for (int i = 0; i < 255; i++)
        buf[i] = 'A' + (i % 26);
    buf[255] = '\0';

    value_t v = make_str(buf);
    check("long_string type", v.type == VAL_CHAR);

    /* Check every byte to find exactly where truncation happens */
    int first_bad = -1;
    for (int i = 0; i < 255; i++) {
        if (v.str[i] != buf[i]) {
            first_bad = i;
            break;
        }
    }
    if (first_bad >= 0) {
        printf("FAIL: long_string truncated at byte %d (expected 0x%02X, got 0x%02X)\n",
               first_bad, (unsigned char)buf[first_bad], (unsigned char)v.str[first_bad]);
        failures++;
    } else {
        check("long_string null terminator", v.str[255] == '\0');
    }
}

/* Test 3: Assign between variables (struct-to-struct copy) */
static void test_assign(void) {
    char buf[256];
    for (int i = 0; i < 255; i++)
        buf[i] = 'Z' - (i % 26);
    buf[255] = '\0';

    value_t a = make_str(buf);
    value_t b;
    b = a;  /* struct assignment */

    check("assign type", b.type == VAL_CHAR);
    int first_bad = -1;
    for (int i = 0; i < 255; i++) {
        if (b.str[i] != buf[i]) {
            first_bad = i;
            break;
        }
    }
    if (first_bad >= 0) {
        printf("FAIL: assign truncated at byte %d (expected 0x%02X, got 0x%02X)\n",
               first_bad, (unsigned char)buf[first_bad], (unsigned char)b.str[first_bad]);
        failures++;
    }
}

/* Test 4: Pass struct by value to a function */
static int verify_value(value_t v, const char *expected) {
    if (v.type != VAL_CHAR) return 0;
    return strcmp(v.str, expected) == 0;
}

static void test_pass_by_value(void) {
    char buf[256];
    for (int i = 0; i < 255; i++)
        buf[i] = '0' + (i % 10);
    buf[255] = '\0';

    value_t v = make_str(buf);
    check("pass_by_value", verify_value(v, buf));
}

/* Test 5: Chain of returns — return from one function feeds into another */
static value_t identity(value_t v) {
    return v;
}

static void test_chain(void) {
    char buf[256];
    for (int i = 0; i < 255; i++)
        buf[i] = 'a' + (i % 26);
    buf[255] = '\0';

    value_t v = identity(identity(make_str(buf)));
    check("chain type", v.type == VAL_CHAR);
    check("chain content", strcmp(v.str, buf) == 0);
}

/* Test 6: Array of structs */
static void test_array(void) {
    value_t arr[4];
    for (int i = 0; i < 4; i++) {
        char buf[256];
        memset(buf, 'A' + i, 255);
        buf[255] = '\0';
        arr[i] = make_str(buf);
    }

    int ok = 1;
    for (int i = 0; i < 4; i++) {
        if (arr[i].type != VAL_CHAR) { ok = 0; break; }
        for (int j = 0; j < 255; j++) {
            if (arr[i].str[j] != 'A' + i) { ok = 0; break; }
        }
        if (!ok) break;
    }
    check("array", ok);
}

/* Test 7: Numeric value round-trip (smaller union member) */
static void test_numeric(void) {
    value_t v = make_num(3.14159);
    check("numeric type", v.type == VAL_NUM);
    /* Simple epsilon check */
    double diff = v.num - 3.14159;
    if (diff < 0) diff = -diff;
    check("numeric value", diff < 0.00001);
}

int main(void) {
    test_short_string();
    test_long_string();
    test_assign();
    test_pass_by_value();
    test_chain();
    test_array();
    test_numeric();

    if (failures == 0)
        printf("PASS: all struct copy tests passed\n");
    else
        printf("FAILED: %d test(s) failed\n", failures);

    return failures ? 1 : 0;
}
