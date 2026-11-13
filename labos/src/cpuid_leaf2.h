// TLB descriptors of CPUID leaf 2. Pure: tests run it on the host.
#ifndef LABOS_CPUID_LEAF2_H
#define LABOS_CPUID_LEAF2_H

#include "efi.h"

#define LEAF2_DATA 0
#define LEAF2_CODE 1
#define LEAF2_UNIFIED 2

// ways 0: not given; full: fully associative (ways then equals entries)
typedef void (*leaf2_sink)(void *context, u32 level, u32 type, const char *pages, u32 entries, u32 ways, int full);

// regs: EAX, EBX, ECX, EDX of leaf 2. Level 1 TLBs come before level 2
void leaf2_tlbs(const u32 regs[4], leaf2_sink sink, void *context);

#endif
