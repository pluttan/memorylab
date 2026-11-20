// The textbook order of the lines for experiment 1 and the T1 and T2 points of its graph.
// Pure: tests run it on the host.
//
// The lab manual expects memory where the banks take turns line by line. An Intel controller picks
// the bank by a XOR of address bits instead, and a growing distance between the reads shows no
// peaks. The bank of a line is a linear function of its address bits: lines x and y share a bank when
// x ^ y lies in one linear subspace, the kernel. labos finds the kernel from the bank of one line and
// lays the lines out in the textbook order: logical line i is line i / banks of bank i % banks.
#ifndef LABOS_TEXTBOOK_H
#define LABOS_TEXTBOOK_H

#include "efi.h"

#define TEXTBOOK_MAX_RANK 32
#define TEXTBOOK_T1_LEVEL 1.2  // T1: the reads at its multiples take this many times the usual time

// in_bank[i] marks the lines measured in the bank of line `ref` (ref included); lines is a power of
// two. A difference of two lines joins the kernel only when most of the bank stays in the bank after
// the shift by it, so a line measured wrongly does not get in. Returns the rank; basis[k] are sorted
// by decreasing value and have distinct leading bits
usize bank_kernel(const u8 *in_bank, u32 lines, u32 ref, u32 *basis);

// Bank numbers of all lines: bank 0 holds line 0, the numbers grow with the first line of a bank.
// Returns the number of banks, lines >> rank. scratch holds `lines` entries
u32 bank_numbers(const u32 *basis, usize rank, u32 lines, u32 *bank_of, u32 *scratch);

// order[i]: the line read as logical line i. The lines of a bank go in address order, so a bank
// gives its rows one after another. scratch holds `banks` entries
void textbook_order(const u32 *bank_of, u32 lines, u32 banks, u32 *order, u32 *scratch);

typedef struct {
    usize t1, t2;     // indices of T1 and T2 in the distances; n when not found
    double fast;      // median time below T2
    double t1_level;  // median time at the odd multiples of T1 below T2, divided by fast
} textbook_peaks;

// dist: growing distances with an even step. T2: the smallest power of two distance with at least
// 0.9 of the largest time. T1: of the power of two distances below T2 whose odd multiples take at
// least TEXTBOOK_T1_LEVEL times the usual time, the one with the largest rise over half of it
textbook_peaks find_textbook_peaks(const u64 *dist, const double *time, usize n, u32 line);

#endif
