// Experiment 3: software prefetch of pages.
#ifndef LABOS_EXP_PREFETCH_H
#define LABOS_EXP_PREFETCH_H

#include "params.h"

// params: read step, B (1..4096); block, KB (4..8192); page order: 1 shuffled, 0 in a row
int prefetch_run(const exp_param *params);
void prefetch_draw(i32 height);
usize prefetch_report(u8 *out, usize cap);

#endif
