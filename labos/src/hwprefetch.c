#include "hwprefetch.h"

#include "efi.h"

#define MSR_PREFETCH 0x1a4u

static u64 saved;
static int changed;

static int known_cpu(void) {
    static const u8 MODELS[] = {0x2a, 0x2d, 0x3a, 0x3e, 0x3c, 0x3f, 0x45, 0x46, 0x3d, 0x47, 0x4f, 0x56,
                                0x4e, 0x5e, 0x55, 0x8e, 0x9e, 0xa5, 0xa6};
    u32 a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    if (b != 0x756e6547u || d != 0x49656e69u || c != 0x6c65746eu) return 0;  // GenuineIntel
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    if (((a >> 8) & 0xf) != 6 || (c >> 31)) return 0;  // family 6, no hypervisor
    u32 model = ((a >> 4) & 0xf) | ((a >> 12) & 0xf0);
    for (usize i = 0; i < sizeof MODELS; i++) {
        if (MODELS[i] == model) return 1;
    }
    return 0;
}

int hw_prefetch_off(void) {
    if (changed) return 1;
    if (!known_cpu()) return 0;
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(MSR_PREFETCH));
    saved = (u64)hi << 32 | lo;
    u64 off = saved | 0xf;
    __asm__ volatile("wrmsr" ::"c"(MSR_PREFETCH), "a"((u32)off), "d"((u32)(off >> 32)));
    changed = 1;
    return 1;
}

void hw_prefetch_restore(void) {
    if (!changed) return;
    __asm__ volatile("wrmsr" ::"c"(MSR_PREFETCH), "a"((u32)saved), "d"((u32)(saved >> 32)));
    changed = 0;
}
