// Host tests of the textbook order on a DRAM model: 16 banks with Skylake style XOR hashing, 8 KB rows,
// a bank measurement with errors, and the classic pass of PCLAB timed on the model.
#include <stdio.h>
#include <stdlib.h>

#include "textbook.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

#define LINES 32768u  // 2 MB of 64 byte lines
#define LINE 64u
#define BANKS 16u
#define POINTS 4096u  // distances 128 B .. 512 KB

static unsigned bank_of_addr(unsigned a) {
    unsigned bit = 0;
    bit |= (((a >> 6) ^ (a >> 13)) & 1) << 0;
    bit |= (((a >> 14) ^ (a >> 18)) & 1) << 1;
    bit |= (((a >> 15) ^ (a >> 19)) & 1) << 2;
    bit |= (((a >> 16) ^ (a >> 20)) & 1) << 3;
    return bit;
}
static unsigned row_of_addr(unsigned a) { return a >> 17; }

static u8 in_bank[LINES];
static u32 bank_of[LINES], order[LINES], scratch[LINES], basis[TEXTBOOK_MAX_RANK];

static void test_kernel(void) {
    // The bank of line 0 with 1 % of the lines measured wrongly both ways
    srand(7);
    for (u32 i = 0; i < LINES; i++) {
        in_bank[i] = bank_of_addr(i * LINE) == bank_of_addr(0);
        if (i && rand() % 100 == 0) in_bank[i] = !in_bank[i];
    }
    in_bank[0] = 1;
    usize rank = bank_kernel(in_bank, LINES, 0, basis);
    CHECK(rank == 11);
    u32 banks = bank_numbers(basis, rank, LINES, bank_of, scratch);
    CHECK(banks == BANKS);
    // The numbers are a renaming of the model banks
    int model_to_ours[BANKS];
    for (u32 k = 0; k < BANKS; k++) model_to_ours[k] = -1;
    int same = 1;
    for (u32 i = 0; i < LINES; i++) {
        unsigned m = bank_of_addr(i * LINE);
        if (model_to_ours[m] < 0) model_to_ours[m] = (int)bank_of[i];
        same &= model_to_ours[m] == (int)bank_of[i];
    }
    CHECK(same);
    CHECK(bank_of[0] == 0);
}

static void test_order(void) {
    textbook_order(bank_of, LINES, BANKS, order, scratch);
    static u8 seen[LINES];
    int perm = 1, turns = 1, ascending = 1;
    for (u32 i = 0; i < LINES; i++) {
        perm &= !seen[order[i]];
        seen[order[i]] = 1;
        turns &= bank_of[order[i]] == i % BANKS;
        if (i >= BANKS) ascending &= order[i] > order[i - BANKS];
    }
    CHECK(perm && turns && ascending);
}

// The classic pass of PCLAB on the model: reads in one bank wait for each other, a row change costs more
static double classic_time(u64 dist) {
    static unsigned open_row[BANKS];
    for (u32 k = 0; k < BANKS; k++) open_row[k] = ~0u;
    unsigned last = ~0u;
    double t = 0;
    for (u64 b = 0; b < dist; b += LINE) {
        for (u64 a = b; a < (u64)LINES * LINE; a += dist) {
            unsigned x = order[a / LINE] * LINE, k = bank_of_addr(x), row = row_of_addr(x);
            int hit = open_row[k] == row;
            t += k == last ? (hit ? 3 : 14) : (hit ? 1 : 5);
            open_row[k] = row, last = k;
        }
    }
    return t;
}

static void test_peaks(void) {
    static u64 dist[POINTS];
    static double time[POINTS];
    for (u32 i = 0; i < POINTS; i++) {
        dist[i] = (u64)(i + 1) * 128;
        time[i] = classic_time(dist[i]) * (1 + (rand() % 100) / 2000.0);
    }
    textbook_peaks p = find_textbook_peaks(dist, time, POINTS, LINE);
    CHECK(p.t1 < POINTS && dist[p.t1] == BANKS * LINE);
    CHECK(p.t2 < POINTS && dist[p.t2] == BANKS * 8192);
    CHECK(p.t1_level > 1.5);
    // A flat graph has no T1
    for (u32 i = 0; i < POINTS; i++) time[i] = 100 + rand() % 3;
    p = find_textbook_peaks(dist, time, POINTS, LINE);
    CHECK(p.t1 == POINTS);
}

int main(void) {
    test_kernel();
    test_order();
    test_peaks();
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
