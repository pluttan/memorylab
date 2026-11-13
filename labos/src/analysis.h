// Analysis of the row conflict measurements. Pure: tests run it on the host.
#ifndef LABOS_ANALYSIS_H
#define LABOS_ANALYSIS_H

#include "efi.h"

// Otsu's threshold between the two groups of a bimodal set; *separation is the gap between the
// group means relative to the lower mean (about 0.5 for DRAM row conflicts, near 0 without them)
double split_threshold(const double *values, usize count, double *separation);

typedef struct {
    usize lines;      // lines measured, the reference included
    usize same_bank;  // lines in the bank of the reference
    usize row_lines;  // lines in the row of the reference, the reference included
    u32 banks;        // lines / same_bank rounded to a power of two
    u32 row_bytes;
} bank_estimate;

// conflict_a[i]: line i conflicts with the reference (line 0); conflict_b[i]: with a second reference
// that conflicts with the first. Their union is the bank, b without a is the row of the reference.
bank_estimate estimate_banks(const u8 *conflict_a, const u8 *conflict_b, usize lines, u32 line_bytes);

#endif
