// Host tests of the row conflict analysis on a synthetic DRAM that behaves like the i5-8400 measurements:
// 16 banks, 8 KB rows, XOR bank hashing, a random tail above the low band and bursts of slow pairs.
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

// One median pair time: 214 or 340 ticks with a little jitter. Interference only adds time: 10 % of
// samples get 20-100 ticks more (the random tail), 2 % about 200 more (the bursts)
static double sample(int is_conflict) {
    double t = (is_conflict ? 340 : 214) + rand() % 12;
    int r = rand() % 100;
    if (r < 2) return t + 200 + rand() % 15;
    if (r < 12) return t + 20 + rand() % 80;
    return t;
}

// What labos does: one pass, then the lines above the low band measured twice more, the smaller time kept
static void measure(unsigned ref, double *t, time_modes *m) {
    for (unsigned i = 0; i < LINES; i++) t[i] = i == ref ? 0 : sample(conflict(ref * 64, i * 64));
    time_modes first = find_modes(t, LINES);
    for (int pass = 0; pass < 2; pass++) {
        for (unsigned i = 0; i < LINES; i++) {
            if (i == ref || t[i] <= first.low * RETRY_ABOVE_LOW) continue;
            double again = sample(conflict(ref * 64, i * 64));
            if (again < t[i]) t[i] = again;
        }
    }
    *m = find_modes(t, LINES);
}

static void test_modes(void) {
    static double t[LINES];
    time_modes m;
    srand(1);
    measure(0, t, &m);
    CHECK(m.low > 210 && m.low < 230);
    CHECK(m.high > 335 && m.high < 360);
    CHECK(m.threshold > 270 && m.threshold < 295);
    CHECK(m.high_count > 1850 && m.high_count < 2000);  // 1920 true conflicts
    CHECK(m.contrast > 10);
    // No conflicts at all: after the second measurements there is no peak, only what is left of the tail
    for (unsigned i = 0; i < LINES; i++) t[i] = sample(0);
    time_modes first = find_modes(t, LINES);
    for (int pass = 0; pass < 2; pass++) {
        for (unsigned i = 0; i < LINES; i++) {
            if (t[i] > first.low * RETRY_ABOVE_LOW) {
                double again = sample(0);
                if (again < t[i]) t[i] = again;
            }
        }
    }
    m = find_modes(t, LINES);
    CHECK(m.high == 0);
    // A burst before the second measurements must not pass for a peak of conflicts either way
    CHECK(first.low > 210 && first.low < 230);
}

static void test_estimate(void) {
    static double ta[LINES], tb[LINES];
    static unsigned char a[LINES], b[LINES];
    time_modes ma, mb;
    srand(2);
    measure(0, ta, &ma);
    // The second reference: a confirmed conflict nearest the centre of the peak
    unsigned ref2 = 0;
    double best = 1e9;
    for (unsigned i = 1; i < LINES; i++) {
        double d = ta[i] > ma.high ? ta[i] - ma.high : ma.high - ta[i];
        if (ta[i] > ma.threshold && d < best) best = d, ref2 = i;
    }
    CHECK(ref2 && conflict(0, ref2 * 64));
    measure(ref2, tb, &mb);
    for (unsigned i = 0; i < LINES; i++) {
        a[i] = i != 0 && ta[i] > ma.threshold;
        b[i] = i != ref2 && tb[i] > mb.threshold;
    }
    bank_estimate e = estimate_banks(a, b, LINES, 64);
    CHECK(e.banks == 16);
    CHECK(e.row_bytes == 8192);
    CHECK(e.same_bank > 1900 && e.same_bank < 2300);
}

static void test_pow2(void) {
    CHECK(nearest_pow2(30.4) == 32);
    CHECK(nearest_pow2(22.0) == 16);
    CHECK(nearest_pow2(23.0) == 32);
    CHECK(nearest_pow2(9536) == 8192);
    CHECK(nearest_pow2(0.3) == 1);
}

int main(void) {
    test_modes();
    test_estimate();
    test_pow2();
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
