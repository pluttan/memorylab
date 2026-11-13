// Experiment 1 on bare hardware: DRAM banks and rows from row conflicts.
//
// Two lines read together take longer when they sit in one bank but in different rows: the bank has to
// close one row and open the other. The experiment reads the reference line together with every line
// of a 2 MB block whose physical addresses are known, so the share of conflicting lines gives the
// number of banks, and a second pass from a conflicting line gives the row of the reference.
#ifndef LABOS_EXP_ROWCONF_H
#define LABOS_EXP_ROWCONF_H

#include "analysis.h"

#define ROWCONF_BLOCK (2u << 20)
#define ROWCONF_LINE 64u
#define ROWCONF_LINES (ROWCONF_BLOCK / ROWCONF_LINE)

typedef struct {
    u64 base;            // physical address of the block
    u32 ref2;            // index of the second reference line
    double ns[ROWCONF_LINES];   // pair time with the reference, ns
    double ns2[ROWCONF_LINES];  // pair time with the second reference, ns
    double threshold, separation;
    bank_estimate estimate;
    u64 memory_bytes;
    u64 pages;  // DRAM pages (rows of a rank) in the whole memory
    u64 duration_us;
} rowconf_result;

// Runs the experiment; rounds: pair reads per line (the median is kept)
int rowconf_run(rowconf_result *r, int rounds);
void rowconf_draw(const rowconf_result *r);
// Text report: conclusions, then "offset ns ns2" per line
usize rowconf_report(const rowconf_result *r, u8 *out, usize cap);

#endif
