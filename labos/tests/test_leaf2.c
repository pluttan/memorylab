// Host test of CPUID leaf 2 decoding on the values of an i5-8400 (the Skylake core of the lab computers).
#include <stdio.h>
#include <string.h>

#include "cpuid_leaf2.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

typedef struct {
    u32 level, type, entries, ways;
    int full;
    const char *pages;
} tlb;

static tlb found[16];
static int count;

static void collect(void *context, u32 level, u32 type, const char *pages, u32 entries, u32 ways, int full) {
    (void)context;
    if (count < 16) found[count++] = (tlb){level, type, entries, ways, full, pages};
}

static const tlb *find(u32 level, u32 type, const char *pages) {
    for (int i = 0; i < count; i++) {
        if (found[i].level == level && found[i].type == type && !strcmp(found[i].pages, pages)) return &found[i];
    }
    return 0;
}

int main(void) {
    // cpuid(2) on pcomp, 13.11.2026: EAX 76036301, EBX 00f0b6ff, ECX 0, EDX 00c30000
    const u32 regs[4] = {0x76036301, 0x00f0b6ff, 0x00000000, 0x00c30000};
    leaf2_tlbs(regs, collect, 0);
    const tlb *dtlb = find(1, LEAF2_DATA, "4 КБ");
    CHECK(dtlb && dtlb->entries == 64 && dtlb->ways == 4 && !dtlb->full);
    const tlb *stlb = find(2, LEAF2_UNIFIED, "4 КБ/2 МБ");
    CHECK(stlb && stlb->entries == 1536 && stlb->ways == 6);
    const tlb *itlb = find(1, LEAF2_CODE, "4 КБ");
    CHECK(itlb && itlb->entries == 128 && itlb->ways == 8);
    const tlb *large = find(1, LEAF2_CODE, "2 МБ/4 МБ");
    CHECK(large && large->full && large->entries == 8);
    CHECK(count == 7);  // 0x63 and 0xc3 give two entries each; 0x01, 0xff, 0xf0 are not TLBs
    CHECK(found[count - 1].level == 2);  // level 1 first
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
