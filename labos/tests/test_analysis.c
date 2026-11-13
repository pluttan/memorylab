// Host tests of the row conflict analysis on a synthetic DRAM: 16 banks, 8 KB rows, XOR bank hashing.
#include <stdio.h>
#include <stdlib.h>

#include "analysis.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

#define LINES 32768  // 2 MB of 64 byte lines

// A single channel, single rank DDR4 module like the lab computers have, Skylake style hashing
static unsigned bank_of(unsigned addr) {
    unsigned bit = 0;
    bit |= (((addr >> 6) ^ (addr >> 13)) & 1) << 0;
    bit |= (((addr >> 14) ^ (addr >> 18)) & 1) << 1;
    bit |= (((addr >> 15) ^ (addr >> 19)) & 1) << 2;
    bit |= (((addr >> 16) ^ (addr >> 20)) & 1) << 3;
    return bit;
}
static unsigned row_of(unsigned addr) { return addr >> 17; }

static int conflict(unsigned a, unsigned b) { return bank_of(a) == bank_of(b) && row_of(a) != row_of(b); }

static void test_threshold(void) {
    static double v[1000];
    srand(1);
    for (int i = 0; i < 1000; i++) v[i] = (i % 16 ? 220 : 340) + rand() % 12;
    double sep, t = split_threshold(v, 1000, &sep);
    CHECK(t > 235 && t < 335);
    CHECK(sep > 0.4);
    for (int i = 0; i < 1000; i++) v[i] = 220 + rand() % 12;  // no conflicts at all
    split_threshold(v, 1000, &sep);
    CHECK(sep < 0.1);
}

static void test_estimate(void) {
    static unsigned char a[LINES], b[LINES];
    unsigned ref2 = 0;
    for (unsigned i = 1; i < LINES && !ref2; i++) {
        if (conflict(0, i * 64)) ref2 = i;
    }
    CHECK(ref2 != 0);
    for (unsigned i = 0; i < LINES; i++) {
        a[i] = (unsigned char)conflict(0, i * 64);
        b[i] = (unsigned char)conflict(ref2 * 64, i * 64);
    }
    bank_estimate e = estimate_banks(a, b, LINES, 64);
    CHECK(e.same_bank == LINES / 16);
    CHECK(e.banks == 16);
    CHECK(e.row_bytes == 8192);
}

int main(void) {
    test_threshold();
    test_estimate();
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
