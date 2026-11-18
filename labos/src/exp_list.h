// Experiment 2: linked and vector structures.
#ifndef LABOS_EXP_LIST_H
#define LABOS_EXP_LIST_H

#include "params.h"

// params: elements, M (1..20); largest fragmentation, KB (4..500); fragmentation step, KB (1..10)
int list_run(const exp_param *params);
void list_draw(i32 height);
usize list_report(u8 *out, usize cap);

#endif
