// Experiment 4: reading several data streams from separate arrays and from one array.
#ifndef LABOS_EXP_STREAMS_H
#define LABOS_EXP_STREAMS_H

#include "params.h"

// params: array size, MB (1..4); largest number of streams (1..128)
int streams_run(const exp_param *params);
void streams_draw(i32 height);
usize streams_report(u8 *out, usize cap);

#endif
