// Host tests of the pure modules: axis arithmetic and formatting.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fmt.h"
#include "plotmath.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

static int near(double a, double b, double eps) { return fabs(a - b) <= eps; }

static void test_nice_step(void) {
    CHECK(near(nice_step(0, 100, 10), 10, 1e-12));
    CHECK(near(nice_step(0, 100, 8), 20, 1e-12));
    CHECK(near(nice_step(0, 1, 4), 0.5, 1e-12));
    CHECK(near(nice_step(0, 3.7, 10), 0.5, 1e-12));
    CHECK(near(nice_step(0, 130000, 12), 20000, 1e-9));
    CHECK(near(nice_step(5, 5, 4), 1, 1e-12));  // empty range does not loop
}

static void test_step_decimals(void) {
    CHECK(step_decimals(10) == 0);
    CHECK(step_decimals(0.5) == 1);
    CHECK(step_decimals(0.25) == 2);
    CHECK(step_decimals(0.2) == 1);
}

static void test_lb(void) {
    CHECK(near(lb(1), 0, 1e-9));
    CHECK(near(lb(1024), 10, 1e-9));
    CHECK(near(lb(3), log2(3), 1e-6));
    CHECK(near(lb(0.375), log2(0.375), 1e-6));
    CHECK(near(lb(9437184), log2(9437184), 1e-6));
}

static void test_axis_pos(void) {
    CHECK(near(axis_pos(5, 0, 10, 0, 100, 200), 150, 1e-9));
    CHECK(near(axis_pos(0, 0, 10, 0, 300, 100), 300, 1e-9));  // y grows upwards
    CHECK(near(axis_pos(4096, 1024, 16384, 1, 0, 400), 200, 1e-6));
}

static void test_format_bytes(void) {
    char s[32];
    format_bytes(s, sizeof s, 512);
    CHECK(!strcmp(s, "512 Б"));
    format_bytes(s, sizeof s, 32768);
    CHECK(!strcmp(s, "32 КБ"));
    format_bytes(s, sizeof s, 1.5 * 1048576);
    CHECK(!strcmp(s, "1,5 МБ"));
}

static void test_fmt(void) {
    char s[64];
    fmt(s, sizeof s, "%d|%5d|%05u|%x|%s|%c", -42, 7, 42u, 255u, "ab", 'z');
    CHECK(!strcmp(s, "-42|    7|00042|ff|ab|z"));
    fmt(s, sizeof s, "%.2f|%.0f|%.3f", 3.14159, 2.5, -0.5);
    CHECK(!strcmp(s, "3.14|3|-0.500"));
    fmt(s, sizeof s, "%lu %ld", 18446744073709551615ull, -9000000000ll);
    CHECK(!strcmp(s, "18446744073709551615 -9000000000"));
    CHECK(fmt(s, 4, "abcdef") == 6 && !strcmp(s, "abc"));  // truncates, reports the full length
}

static void test_utf8(void) {
    const char *p = "Яz";
    CHECK(utf8_next(&p) == 0x42f);
    CHECK(utf8_next(&p) == 'z');
    CHECK(*p == 0);
}

int main(void) {
    test_nice_step();
    test_step_decimals();
    test_lb();
    test_axis_pos();
    test_format_bytes();
    test_fmt();
    test_utf8();
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
