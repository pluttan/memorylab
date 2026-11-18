#include "analysis.h"

#define BINS 600
#define HIGH_RATIO 1.25   // the conflict peak sits at least this far above the low band
#define PEAK_WINDOW 0.06  // lines within this share of the high peak belong to it
#define MIN_CONTRAST 4.0
#define MIN_PEAK_LINES(n) ((n) / 2048 + 4)  // 512 banks at most: a real peak holds n / 512 lines

void histogram(const double *v, usize n, double lo, double hi, u32 *bins, usize nbins) {
    for (usize b = 0; b < nbins; b++) bins[b] = 0;
    if (hi <= lo || !nbins) return;
    double width = (hi - lo) / (double)nbins;
    for (usize i = 0; i < n; i++) {
        double x = (v[i] - lo) / width;
        usize b = x < 0 ? 0 : x >= (double)nbins ? nbins - 1 : (usize)x;
        bins[b]++;
    }
}

time_modes find_modes(const double *v, usize n) {
    time_modes m = {0, 0, 0, 0, 0};
    double lo = 0;
    for (usize i = 0; i < n; i++) {
        if (v[i] > 0 && (lo == 0 || v[i] < lo)) lo = v[i];
    }
    if (lo <= 0) return m;
    // Row conflicts take about 1.5 times as long; four times the fastest pair covers them with room.
    // Slower pairs fall into the last bin, which is not searched
    static u32 bins[BINS];
    double hi = lo * 4, width = (hi - lo) / BINS;
    histogram(v, n, lo, hi, bins, BINS);
    usize low_bin = 0;
    for (usize b = 0; b + 1 < BINS; b++) {
        if (bins[b] > bins[low_bin]) low_bin = b;
    }
    m.low = lo + (low_bin + 0.5) * width;
    usize high_bin = 0;
    for (usize b = low_bin + 1; b + 1 < BINS; b++) {
        if (lo + (b + 0.5) * width < m.low * HIGH_RATIO) continue;
        if (!high_bin || bins[b] > bins[high_bin]) high_bin = b;
    }
    if (!high_bin || !bins[high_bin]) return m;
    double high = lo + (high_bin + 0.5) * width;
    double peak_lo = high * (1 - PEAK_WINDOW), peak_hi = high * (1 + PEAK_WINDOW);
    double gap_lo = m.low * RETRY_ABOVE_LOW, gap_hi = peak_lo;
    usize in_peak = 0, in_gap = 0;
    for (usize i = 0; i < n; i++) {
        if (v[i] >= peak_lo && v[i] <= peak_hi) in_peak++;
        if (v[i] > gap_lo && v[i] < gap_hi) in_gap++;
    }
    // A peak of row conflicts stands out of the gap below it; a flat tail of noise does not
    double peak_density = (double)in_peak / (peak_hi - peak_lo);
    double gap_density = gap_hi > gap_lo ? (double)in_gap / (gap_hi - gap_lo) : 0;
    m.contrast = gap_density > 0 ? peak_density / gap_density : peak_density > 0 ? 1e9 : 0;
    m.high_count = in_peak;
    if (in_peak < MIN_PEAK_LINES(n) || m.contrast < MIN_CONTRAST) return m;
    m.high = high;
    m.threshold = (m.low + m.high) / 2;
    return m;
}

u64 nearest_pow2(double v) {
    u64 p = 1;
    while ((double)p * 2 <= v * 1.4142135623730951) p *= 2;
    return p;
}

bank_estimate estimate_banks(const u8 *a, const u8 *b, usize lines, u32 line_bytes) {
    bank_estimate e = {lines, 0, 0, 0, 0};
    for (usize i = 0; i < lines; i++) {
        if (a[i] || b[i]) e.same_bank++;
        if (b[i] && !a[i]) e.row_lines++;
    }
    // Banks and rows come in powers of two; the few lines noise adds or takes away vanish in the rounding
    if (e.same_bank) e.banks = (u32)nearest_pow2((double)lines / (double)e.same_bank);
    if (e.row_lines) e.row_bytes = (u32)nearest_pow2((double)(e.row_lines * line_bytes));
    return e;
}
