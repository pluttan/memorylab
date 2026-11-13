// Experiment 1 on bare hardware: DRAM banks and rows from row conflicts.
//
// Two lines read together take longer when they sit in one bank but in different rows: the bank has to
// close one row and open the other. The experiment reads the reference line together with every line
// of a block whose physical addresses are known, so the share of conflicting lines gives the number of
// banks, and a second pass from a conflicting line gives the row of the reference.
#ifndef LABOS_EXP_ROWCONF_H
#define LABOS_EXP_ROWCONF_H

#include "params.h"

#define ROWCONF_LINE 64u

// params[0]: block size, MB (2..16); params[1]: pair reads per line, the median is kept (1..63)
int rowconf_run(const exp_param *params);
void rowconf_draw(i32 height);
// Conclusions, then "offset ns ns2" per line
usize rowconf_report(u8 *out, usize cap);

#endif
