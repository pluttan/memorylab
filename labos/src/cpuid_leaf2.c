#include "cpuid_leaf2.h"

#define WAYS_UNKNOWN 0xffff

// Intel SDM descriptor table for CPUID leaf 2 (Intel before Ice Lake has no leaf 0x18)
static const struct {
    u8 code;
    u8 level;
    u8 type;  // 0 data, 1 instructions, 2 unified
    const char *pages;
    u16 entries, ways;  // ways 0: fully associative
} LEAF2[] = {
    {0x01, 1, 1, "4 КБ", 32, 4},          {0x02, 1, 1, "4 МБ", 2, 0},
    {0x03, 1, 0, "4 КБ", 64, 4},          {0x04, 1, 0, "4 МБ", 8, 4},
    {0x05, 1, 0, "4 МБ", 32, 4},          {0x0b, 1, 1, "4 МБ", 4, 4},
    {0x4f, 1, 1, "4 КБ", 32, WAYS_UNKNOWN}, {0x50, 1, 1, "4 КБ/2 МБ/4 МБ", 64, WAYS_UNKNOWN},
    {0x51, 1, 1, "4 КБ/2 МБ/4 МБ", 128, WAYS_UNKNOWN}, {0x52, 1, 1, "4 КБ/2 МБ/4 МБ", 256, WAYS_UNKNOWN},
    {0x55, 1, 1, "2 МБ/4 МБ", 7, 0},      {0x56, 1, 0, "4 МБ", 16, 4},
    {0x57, 1, 0, "4 КБ", 16, 4},          {0x59, 1, 0, "4 КБ", 16, 0},
    {0x5a, 1, 0, "2 МБ/4 МБ", 32, 4},     {0x5b, 1, 0, "4 КБ/4 МБ", 64, WAYS_UNKNOWN},
    {0x5c, 1, 0, "4 КБ/4 МБ", 128, WAYS_UNKNOWN}, {0x5d, 1, 0, "4 КБ/4 МБ", 256, WAYS_UNKNOWN},
    {0x61, 1, 1, "4 КБ", 48, 0},          {0x63, 1, 0, "2 МБ/4 МБ", 32, 4},
    {0x63, 1, 0, "1 ГБ", 4, 4},           {0x64, 1, 0, "4 КБ", 512, 4},
    {0x6a, 1, 0, "4 КБ", 64, 8},          {0x6b, 1, 0, "4 КБ", 256, 8},
    {0x6c, 1, 0, "2 МБ/4 МБ", 128, 8},    {0x6d, 1, 0, "1 ГБ", 16, 0},
    {0x76, 1, 1, "2 МБ/4 МБ", 8, 0},      {0xa0, 1, 0, "4 КБ", 32, 0},
    {0xb0, 1, 1, "4 КБ", 128, 4},         {0xb1, 1, 1, "2 МБ", 8, 4},
    {0xb2, 1, 1, "4 КБ", 64, 4},          {0xb3, 1, 0, "4 КБ", 128, 4},
    {0xb4, 1, 0, "4 КБ", 256, 4},         {0xb5, 1, 1, "4 КБ", 64, 8},
    {0xb6, 1, 1, "4 КБ", 128, 8},         {0xba, 1, 0, "4 КБ", 64, 4},
    {0xc0, 1, 0, "4 КБ/4 МБ", 8, 4},      {0xc1, 2, 2, "4 КБ/2 МБ", 1024, 8},
    {0xc2, 1, 0, "4 КБ/2 МБ", 16, 4},     {0xc3, 2, 2, "4 КБ/2 МБ", 1536, 6},
    {0xc3, 2, 2, "1 ГБ", 16, 4},          {0xc4, 1, 0, "2 МБ/4 МБ", 32, 4},
    {0xca, 2, 2, "4 КБ", 512, 4},
};

void leaf2_tlbs(const u32 regs[4], leaf2_sink sink, void *context) {
    for (u32 level = 1; level <= 2; level++) {
        for (int k = 0; k < 4; k++) {
            if (regs[k] & 0x80000000u) continue;  // no descriptors in this register
            for (int byte = k == 0 ? 1 : 0; byte < 4; byte++) {  // the low byte of EAX is a count
                u8 code = (u8)(regs[k] >> (8 * byte));
                for (usize i = 0; i < sizeof LEAF2 / sizeof LEAF2[0]; i++) {
                    if (LEAF2[i].code != code || LEAF2[i].level != level) continue;
                    u16 ways = LEAF2[i].ways;
                    sink(context, level, LEAF2[i].type, LEAF2[i].pages, LEAF2[i].entries,
                         ways == WAYS_UNKNOWN ? 0 : ways, ways == 0);
                }
            }
        }
    }
}
