// Analysis of the row conflict measurements. Pure: tests run it on the host.
#ifndef LABOS_ANALYSIS_H
#define LABOS_ANALYSIS_H

#include "efi.h"

// The two groups of pair times on real hardware: a wide low band (other bank or the same row) with a
// random tail, and a narrow peak of row conflicts about 1.5 times higher. Found on a histogram:
// low is its highest bin, high the highest bin at least 25 % above low. Interference only adds time,
// so the tail goes away when the lines above the low band are measured again and the smaller time kept
typedef struct {
    double low, high;   // centres of the two peaks; high is 0 when there is no clear second peak
    double threshold;   // halfway between them
    usize high_count;   // lines within 6 % of the high peak
    double contrast;    // lines per tick in the peak / lines per tick in the gap below it
} time_modes;

time_modes find_modes(const double *values, usize count);

// Lines above this are measured again: the tail of the low band starts here
#define RETRY_ABOVE_LOW 1.12

// Histogram of count values into bins of [lo, hi); values outside go to the edge bins
void histogram(const double *values, usize count, double lo, double hi, u32 *bins, usize nbins);

typedef struct {
    usize lines;      // lines measured, the reference included
    usize same_bank;  // lines in the bank of the reference
    usize row_lines;  // lines in the row of the reference, the reference included
    u32 banks;        // lines / same_bank rounded to a power of two
    u32 row_bytes;    // row_lines * line size rounded to a power of two
} bank_estimate;

// conflict_a[i]: line i conflicts with the reference (line 0); conflict_b[i]: with a second reference
// that conflicts with the first. Their union is the bank, b without a is the row of the reference.
bank_estimate estimate_banks(const u8 *conflict_a, const u8 *conflict_b, usize lines, u32 line_bytes);

// The power of two nearest to v on a log scale (1 for v < 1)
u64 nearest_pow2(double v);

#endif
