#include "textbook.h"

#define MAX_POINTS 16384
#define T2_SHARE 0.9   // T2: the time comes this near the largest one
#define RISE_MARGIN 1.1  // a larger distance becomes T1 only with a clearly larger rise

// With the basis sorted by decreasing value, v ^ b < v exactly when v has the leading bit of b:
// the result has none of the leading bits, the same for every line of one coset
static u32 reduce(u32 v, const u32 *basis, usize rank) {
    for (usize k = 0; k < rank; k++) {
        if ((v ^ basis[k]) < v) v ^= basis[k];
    }
    return v;
}

usize bank_kernel(const u8 *in_bank, u32 lines, u32 ref, u32 *basis) {
    usize rank = 0, members = 0;
    for (u32 i = 0; i < lines; i++) members += in_bank[i] != 0;
    for (u32 m = 0; m < lines && rank < TEXTBOOK_MAX_RANK; m++) {
        if (!in_bank[m] || m == ref) continue;
        u32 v = reduce(m ^ ref, basis, rank);
        if (!v) continue;
        usize kept = 0;
        for (u32 x = 0; x < lines; x++) kept += in_bank[x] && in_bank[x ^ v];
        if (kept * 2 < members) continue;
        usize k = rank++;
        for (; k > 0 && basis[k - 1] < v; k--) basis[k] = basis[k - 1];
        basis[k] = v;
    }
    return rank;
}

u32 bank_numbers(const u32 *basis, usize rank, u32 lines, u32 *bank_of, u32 *scratch) {
    for (u32 i = 0; i < lines; i++) scratch[i] = ~0u;
    u32 banks = 0;
    for (u32 i = 0; i < lines; i++) {
        u32 r = reduce(i, basis, rank);
        if (scratch[r] == ~0u) scratch[r] = banks++;
        bank_of[i] = scratch[r];
    }
    return banks;
}

void textbook_order(const u32 *bank_of, u32 lines, u32 banks, u32 *order, u32 *scratch) {
    for (u32 k = 0; k < banks; k++) scratch[k] = 0;
    for (u32 x = 0; x < lines; x++) {
        u32 k = bank_of[x];
        u64 i = (u64)scratch[k]++ * banks + k;
        if (i < lines) order[i] = x;
    }
}

static double work[MAX_POINTS], picked[MAX_POINTS];

static double median(const double *v, usize n) {
    for (usize i = 0; i < n; i++) work[i] = v[i];
    i64 lo = 0, hi = (i64)n - 1, k = (i64)n / 2;
    while (lo < hi) {
        double pivot = work[(lo + hi) / 2];
        i64 i = lo, j = hi;
        while (i <= j) {
            while (work[i] < pivot) i++;
            while (work[j] > pivot) j--;
            if (i <= j) {
                double t = work[i];
                work[i++] = work[j], work[j--] = t;
            }
        }
        if (k <= j) {
            hi = j;
        } else if (k >= i) {
            lo = i;
        } else {
            break;
        }
    }
    return work[k];
}

static int pow2(u64 v) { return v && !(v & (v - 1)); }

textbook_peaks find_textbook_peaks(const u64 *dist, const double *time, usize n, u32 line) {
    textbook_peaks p = {n, n, 0, 0};
    if (n < 4 || n > MAX_POINTS || dist[1] <= dist[0]) return p;
    u64 step = dist[1] - dist[0];
    double top = 0;
    for (usize i = 0; i < n; i++) top = time[i] > top ? time[i] : top;
    for (usize i = 1; i < n && p.t2 == n; i++) {
        if (pow2(dist[i]) && time[i] >= T2_SHARE * top) p.t2 = i;
    }
    if (p.t2 == n) return p;
    p.fast = median(time, p.t2);
    if (p.fast <= 0) return p;
    double prev_level = 1, best_rise = 0;
    for (u64 d1 = 2ull * line; d1 < dist[p.t2]; d1 *= 2) {
        if (d1 < dist[0] || (d1 - dist[0]) % step) continue;
        usize m = 0;
        for (u64 d = d1; d < dist[p.t2]; d += 2 * d1) picked[m++] = time[(d - dist[0]) / step];
        double level = median(picked, m) / p.fast;
        double rise = level / prev_level;
        if (level >= TEXTBOOK_T1_LEVEL && rise > best_rise * RISE_MARGIN) {
            best_rise = rise;
            p.t1 = (usize)((d1 - dist[0]) / step);
            p.t1_level = level;
        }
        prev_level = level > 1 ? level : 1;
    }
    return p;
}
