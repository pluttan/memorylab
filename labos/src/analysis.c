#include "analysis.h"

#define BINS 256

double split_threshold(const double *v, usize n, double *separation) {
    *separation = 0;
    if (n < 2) return 0;
    double lo = v[0], hi = v[0];
    for (usize i = 1; i < n; i++) {
        if (v[i] < lo) lo = v[i];
        if (v[i] > hi) hi = v[i];
    }
    if (hi <= lo) return hi;
    // Otsu: the cut that maximizes the variance between the two groups
    usize hist[BINS] = {0};
    double width = (hi - lo) / BINS;
    for (usize i = 0; i < n; i++) {
        usize b = (usize)((v[i] - lo) / width);
        hist[b < BINS ? b : BINS - 1]++;
    }
    double total = 0;
    for (usize b = 0; b < BINS; b++) total += (double)hist[b] * (lo + (b + 0.5) * width);
    double best = -1, first_cut = lo, last_cut = lo, below_sum = 0;
    usize below = 0;
    for (usize b = 0; b + 1 < BINS; b++) {
        below += hist[b];
        below_sum += (double)hist[b] * (lo + (b + 0.5) * width);
        if (!below || below == n) continue;
        double m0 = below_sum / below, m1 = (total - below_sum) / (n - below);
        double between = (double)below * (n - below) * (m1 - m0) * (m1 - m0);
        if (between > best * (1 + 1e-12)) {
            best = between;
            first_cut = last_cut = lo + (b + 1) * width;
            *separation = m0 > 0 ? (m1 - m0) / m0 : 0;
        } else if (between >= best * (1 - 1e-12)) {
            // Every cut inside an empty gap scores the same; the middle of the gap reads best on a graph
            last_cut = lo + (b + 1) * width;
        }
    }
    return (first_cut + last_cut) / 2;
}

bank_estimate estimate_banks(const u8 *a, const u8 *b, usize lines, u32 line_bytes) {
    bank_estimate e = {lines, 0, 0, 0, 0};
    for (usize i = 0; i < lines; i++) {
        if (a[i] || b[i]) e.same_bank++;
        if (b[i] && !a[i]) e.row_lines++;
    }
    if (e.same_bank) {
        // Bank counts are powers of two; round the ratio on a log scale
        double ratio = (double)lines / e.same_bank;
        u32 banks = 1;
        while (banks * 2.0 <= ratio * 1.4142) banks *= 2;
        e.banks = banks;
    }
    e.row_bytes = (u32)(e.row_lines * line_bytes);
    return e;
}
