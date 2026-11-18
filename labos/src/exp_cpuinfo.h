// Tasks 1 and 2: processor identification by CPUID, memory size by the UEFI memory map.
#ifndef LABOS_EXP_CPUINFO_H
#define LABOS_EXP_CPUINFO_H

#include "params.h"

int cpuinfo_run(const exp_param *params);
void cpuinfo_draw(i32 height);
usize cpuinfo_report(u8 *out, usize cap);

// Size of the data (or unified) cache of a level by CPUID, 0 when the processor does not say;
// the last level when level is 0. Reads CPUID on first use
u64 cpu_cache_bytes(int level);
u32 cpu_line_bytes(void);

#endif
