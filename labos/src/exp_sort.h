// Experiment 7: comparison of sorting algorithms.
#ifndef LABOS_EXP_SORT_H
#define LABOS_EXP_SORT_H

#include "params.h"

// params: largest number of elements, M (1..20); length step of the array, K (4..1024)
int sort_run(const exp_param *params);
void sort_draw(i32 height);
usize sort_report(u8 *out, usize cap);

#endif
