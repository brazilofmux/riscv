/* Soft-float math library tests */
#include <stdio.h>
#include <math.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void check_approx(double got, double expected, double tol, const char *name) {
    double err = fabs(got - expected);
    if (err < tol) {
        printf("PASS: %s\n", name);
        tests_passed++;
    } else {
        printf("FAIL: %s (got %.10f, expected %.10f, err %.2e)\n",
               name, got, expected, err);
        tests_failed++;
    }
}

static void check(int cond, const char *name) {
    if (cond) {
        printf("PASS: %s\n", name);
        tests_passed++;
    } else {
        printf("FAIL: %s\n", name);
        tests_failed++;
    }
}

#define TOL 1e-6

int main(void) {
    printf("--- math tests ---\n");

    /* fabs */
    check_approx(fabs(-5.0), 5.0, TOL, "fabs(-5)");
    check_approx(fabs(3.14), 3.14, TOL, "fabs(3.14)");
    check_approx(fabs(0.0), 0.0, TOL, "fabs(0)");

    /* sqrt */
    check_approx(sqrt(4.0), 2.0, TOL, "sqrt(4)");
    check_approx(sqrt(2.0), 1.414214, TOL, "sqrt(2)");
    check_approx(sqrt(0.0), 0.0, TOL, "sqrt(0)");
    check_approx(sqrt(1.0), 1.0, TOL, "sqrt(1)");

    /* sin/cos */
    check_approx(sin(0.0), 0.0, TOL, "sin(0)");
    check_approx(sin(M_PI / 2.0), 1.0, TOL, "sin(pi/2)");
    check_approx(sin(M_PI), 0.0, TOL, "sin(pi)");
    check_approx(cos(0.0), 1.0, TOL, "cos(0)");
    check_approx(cos(M_PI), -1.0, TOL, "cos(pi)");
    check_approx(cos(M_PI / 2.0), 0.0, TOL, "cos(pi/2)");

    /* tan */
    check_approx(tan(0.0), 0.0, TOL, "tan(0)");
    check_approx(tan(M_PI / 4.0), 1.0, TOL, "tan(pi/4)");

    /* exp/log */
    check_approx(exp(0.0), 1.0, TOL, "exp(0)");
    check_approx(exp(1.0), 2.718282, TOL, "exp(1)");
    check_approx(log(1.0), 0.0, TOL, "log(1)");
    check_approx(log(M_E), 1.0, TOL, "log(e)");
    check_approx(log10(100.0), 2.0, TOL, "log10(100)");
    check_approx(log2(8.0), 3.0, TOL, "log2(8)");

    /* pow */
    check_approx(pow(2.0, 10.0), 1024.0, TOL, "pow(2,10)");
    check_approx(pow(3.0, 0.0), 1.0, TOL, "pow(3,0)");
    check_approx(pow(2.0, 0.5), 1.414214, TOL, "pow(2,0.5)");

    /* atan/atan2 */
    check_approx(atan(1.0), M_PI / 4.0, TOL, "atan(1)");
    check_approx(atan2(1.0, 1.0), M_PI / 4.0, TOL, "atan2(1,1)");
    check_approx(atan2(0.0, 1.0), 0.0, TOL, "atan2(0,1)");
    check_approx(atan2(1.0, 0.0), M_PI / 2.0, TOL, "atan2(1,0)");

    /* asin/acos */
    check_approx(asin(0.0), 0.0, TOL, "asin(0)");
    check_approx(asin(1.0), M_PI / 2.0, TOL, "asin(1)");
    check_approx(acos(1.0), 0.0, TOL, "acos(1)");
    check_approx(acos(0.0), M_PI / 2.0, TOL, "acos(0)");

    /* floor/ceil/round/trunc */
    check_approx(floor(3.7), 3.0, TOL, "floor(3.7)");
    check_approx(floor(-3.2), -4.0, TOL, "floor(-3.2)");
    check_approx(ceil(3.2), 4.0, TOL, "ceil(3.2)");
    check_approx(ceil(-3.7), -3.0, TOL, "ceil(-3.7)");
    check_approx(round(3.5), 4.0, TOL, "round(3.5)");
    check_approx(trunc(3.9), 3.0, TOL, "trunc(3.9)");
    check_approx(trunc(-3.9), -3.0, TOL, "trunc(-3.9)");

    /* fmod */
    check_approx(fmod(5.3, 2.0), 1.3, TOL, "fmod(5.3,2)");

    /* copysign */
    check_approx(copysign(3.0, -1.0), -3.0, TOL, "copysign(3,-1)");
    check_approx(copysign(-3.0, 1.0), 3.0, TOL, "copysign(-3,1)");

    /* ldexp/frexp */
    check_approx(ldexp(1.0, 10), 1024.0, TOL, "ldexp(1,10)");
    {
        int exp;
        double frac = frexp(8.0, &exp);
        check_approx(frac, 0.5, TOL, "frexp(8) frac");
        check(exp == 4, "frexp(8) exp");
    }

    /* hyperbolic */
    check_approx(sinh(0.0), 0.0, TOL, "sinh(0)");
    check_approx(cosh(0.0), 1.0, TOL, "cosh(0)");
    check_approx(tanh(0.0), 0.0, TOL, "tanh(0)");

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed;
}
