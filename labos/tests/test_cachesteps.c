// Host tests of the block sizes and of the step search on a model of a three level cache.
#include <stdio.h>

#include "cachesteps.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

#define CAP 1024

// Time per kilobyte of a cache: flat inside a level, a blurred rise after its size
static double model(u64 size) {
    const u64 level[3] = {32u << 10, 256u << 10, 6u << 20};
    const double flat[4] = {10, 18, 30, 90};
    double t = flat[0];
    for (int l = 0; l < 3; l++) {
        if (size > level[l] * 5 / 4) t = flat[l + 1];
        else if (size > level[l]) t = flat[l] + (flat[l + 1] - flat[l]) * (double)(size - level[l]) / (double)(level[l] / 4);
    }
    return t;
}

int main(void) {
    static u64 sizes[CAP];
    static double time[CAP];
    usize n = cache_block_sizes(1024, 128u << 20, 64, sizes, CAP);
    CHECK(n > 100 && n < 400);
    CHECK(sizes[0] == 1024 && sizes[1] == 2048);
    int lines_ok = 1, grows = 1;
    for (usize i = 0; i < n; i++) {
        lines_ok &= sizes[i] % 64 == 0;
        if (i) grows &= sizes[i] > sizes[i - 1];
    }
    CHECK(lines_ok && grows);
    CHECK(sizes[n - 1] <= (128u << 20));
    // A step of one byte still moves by whole lines
    u64 small[8];
    CHECK(cache_block_sizes(1, 1024, 64, small, 8) == 8 && small[0] == 64 && small[1] == 128);

    for (usize i = 0; i < n; i++) time[i] = model(sizes[i]);
    u64 bounds[3];
    usize found = cache_steps(sizes, time, n, bounds);
    CHECK(found == 3);
    // Every bound lies within a quarter of an octave of the cache size
    const u64 level[3] = {32u << 10, 256u << 10, 6u << 20};
    for (usize i = 0; i < found && i < 3; i++) {
        CHECK(bounds[i] >= level[i] * 3 / 4 && bounds[i] <= level[i] * 5 / 4);
    }
    // A flat graph has no steps
    for (usize i = 0; i < n; i++) time[i] = 10;
    CHECK(cache_steps(sizes, time, n, bounds) == 0);
    CHECK(cache_steps(sizes, time, 3, bounds) == 0);

    if (!failures) printf("ok\n");
    return failures != 0;
}
