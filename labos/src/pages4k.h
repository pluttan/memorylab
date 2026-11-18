// 4 KB pages for experiment 3. UEFI maps memory one to one with 2 MB and 1 GB pages, so a block of a few
// megabytes takes one or two TLB entries and no page boundary costs anything. For the experiment labos
// copies the page tables on the way to the block, splits the large pages that cover it into 4 KB ones
// and loads the copy into CR3. Addresses stay the same; only the page size of the block changes.
#ifndef LABOS_PAGES4K_H
#define LABOS_PAGES4K_H

#include "efi.h"

// Maps [base, base + size) with 4 KB pages (base and size multiples of 2 MB); 0 when the page tables are
// not the expected four levels, and then nothing changed
int pages4k_begin(u64 base, u64 size);
// Back to the firmware page tables
void pages4k_end(void);

#endif
