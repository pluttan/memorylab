// Experiment 6: performance of the cache memory.
#ifndef LABOS_EXP_CACHEPERF_H
#define LABOS_EXP_CACHEPERF_H

#include "params.h"

// params: largest block, KB (1..1048576); growth step of the block, B (1..4096)
int cacheperf_run(const exp_param *params);
void cacheperf_draw(i32 height);
usize cacheperf_report(u8 *out, usize cap);

#endif
