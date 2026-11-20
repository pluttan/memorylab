// Experiment 1 on bare hardware: DRAM banks and rows, and the classic graph of the lab manual.
//
// Two lines read together take longer when they sit in one bank but in different rows: the bank has to
// close one row and open the other. The experiment reads the reference line together with every line
// of a block whose physical addresses are known, so the share of conflicting lines gives the number of
// banks, and a second pass from a conflicting line gives the row of the reference. From these two
// passes labos finds the bank of every line and lays the lines out so that the banks take turns line
// by line. The classic PCLAB pass over this order shows T1 and T2 where the manual expects them.
#ifndef LABOS_EXP_ROWCONF_H
#define LABOS_EXP_ROWCONF_H

#include "params.h"

#define ROWCONF_LINE 64u

// params: block size, MB (2..16, a power of two); pair reads per line, the median is kept (1..63);
// largest distance of the classic pass, KB (1..1024); its step, B (64..4096)
int rowconf_run(const exp_param *params);
void rowconf_draw(i32 height);
// Conclusions, then "distance time_us" of the classic pass, then "offset ns ns2" per line
usize rowconf_report(u8 *out, usize cap);

#endif
