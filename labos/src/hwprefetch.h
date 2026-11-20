// The hardware prefetchers of Intel Core processors, switched off for the time of a measurement.
#ifndef LABOS_HWPREFETCH_H
#define LABOS_HWPREFETCH_H

// Switches the four prefetchers off (L2 stream, L2 adjacent line, L1 stream, L1 by instruction);
// 1 when they are off. Only the client models from Sandy Bridge to Comet Lake on bare hardware:
// elsewhere MSR 0x1A4 may be absent or laid out otherwise, and labos leaves it alone
int hw_prefetch_off(void);
// Puts back what hw_prefetch_off found; does nothing when it changed nothing
void hw_prefetch_restore(void);

#endif
