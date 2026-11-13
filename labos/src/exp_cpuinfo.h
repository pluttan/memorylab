// Tasks 1 and 2: processor identification by CPUID, memory size by the UEFI memory map.
#ifndef LABOS_EXP_CPUINFO_H
#define LABOS_EXP_CPUINFO_H

#include "params.h"

int cpuinfo_run(const exp_param *params);
void cpuinfo_draw(i32 height);
usize cpuinfo_report(u8 *out, usize cap);

#endif
