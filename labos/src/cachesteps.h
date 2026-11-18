// Block sizes and the steps of the cache performance graph (experiment 6). Pure: tests run it on the host.
#ifndef LABOS_CACHESTEPS_H
#define LABOS_CACHESTEPS_H

#include "efi.h"

#define CACHE_GROWTH 1.0905  // 2^(1/8): eight points for every doubling of the block
#define CACHE_JUMP 1.3       // a step: the time half an octave later is this many times longer

// Sizes from `step` by `step`, then by eight points per doubling once that grows faster; all are
// whole lines. Returns how many were written to `sizes` (at most `cap`)
usize cache_block_sizes(u64 step, u64 max_bytes, u64 line, u64 *sizes, usize cap);

// Up to three cache sizes in increasing order where the time per kilobyte rises in steps. The rise at
// point i is time[i + 4] / time[i]: a blurred step fits into this window. Only the largest rise of
// every octave counts, and of the rises above CACHE_JUMP the three largest are taken. The bound is the
// block size before the steepest point inside the window. Returns how many were found
usize cache_steps(const u64 *sizes, const double *time, usize n, u64 bounds[3]);

#endif
