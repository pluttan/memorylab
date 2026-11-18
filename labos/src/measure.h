// Timing and isolation helpers of the experiments: TSC with fences, interrupts off, caches and TLB flushed.
// labos runs in ring 0, so wbinvd and a CR3 reload are available, unlike under an operating system.
#ifndef LABOS_MEASURE_H
#define LABOS_MEASURE_H

#include "lib.h"

static inline u64 tsc_begin(void) {
    __asm__ volatile("lfence" ::: "memory");
    u64 t = rdtsc();
    __asm__ volatile("lfence" ::: "memory");
    return t;
}

static inline u64 tsc_end(void) {
    __asm__ volatile("lfence" ::: "memory");
    return rdtsc();
}

static inline double ticks_ns(u64 ticks) { return (double)ticks / tsc_per_ns; }

// Keeps a value alive so that the compiler does not drop the loop that made it
static inline void keep(u64 v) { __asm__ volatile("" ::"r"(v) : "memory"); }

static inline void flush_caches(void) { __asm__ volatile("wbinvd" ::: "memory"); }

static inline void flush_tlb(void) {
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");
}

// Interrupts off: firmware timers and USB polling wait until quiet_end
static inline usize quiet_begin(void) { return BS->raise_tpl(31); }
static inline void quiet_end(usize old) { BS->restore_tpl(old); }

// The status line shows how far a long experiment has got; cheap to call often
void progress(u64 done, u64 total);

// A small linear congruential generator: the same shuffles in every run
static inline u32 lcg_next(u64 *state) {
    *state = *state * 6364136223846793005ull + 1442695040888963407ull;
    return (u32)(*state >> 33);
}

#endif
