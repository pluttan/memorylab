// Experiment 5: conflicts in the cache memory.
#ifndef LABOS_EXP_CONFLICTS_H
#define LABOS_EXP_CONFLICTS_H

#include "params.h"

// params: bank size, KB (1..256); line size, B (1..128); largest number of lines (2..512)
int conflicts_run(const exp_param *params);
void conflicts_draw(i32 height);
usize conflicts_report(u8 *out, usize cap);

#endif
