#include "exp_cpuinfo.h"

#include "cpuid_leaf2.h"
#include "gfx.h"
#include "lib.h"
#include "plotmath.h"
#include "widgets.h"

#define MAX_CACHES 8
#define MAX_TLBS 16

typedef struct {
    u32 level;
    const char *type;
    u64 size;
    u32 line, ways, sets;
    int full;
} cache_info;

typedef struct {
    u32 level;
    const char *type;
    const char *pages;
    u32 entries, ways;  // ways 0: not given by the processor
    int full;
} tlb_info;

static struct {
    char vendor[13], brand[49];
    u32 family, model, stepping;
    cache_info caches[MAX_CACHES];
    tlb_info tlbs[MAX_TLBS];
    int ncaches, ntlbs;
    u64 memory_bytes;
    double tsc_ghz;
    int valid;
} C;

typedef struct {
    u32 a, b, c, d;
} regs;

static regs cpuid(u32 leaf, u32 subleaf) {
    regs r;
    __asm__ volatile("cpuid" : "=a"(r.a), "=b"(r.b), "=c"(r.c), "=d"(r.d) : "a"(leaf), "c"(subleaf));
    return r;
}

static const char *const TYPE_DATA = "данные", *const TYPE_CODE = "команды", *const TYPE_UNIFIED = "общий";

// ==========================
// ===  Identity, caches  ===
// ==========================

static void read_identity(void) {
    regs r = cpuid(0, 0);
    memcpy(C.vendor, &r.b, 4);
    memcpy(C.vendor + 4, &r.d, 4);
    memcpy(C.vendor + 8, &r.c, 4);
    C.vendor[12] = 0;
    u32 sig = cpuid(1, 0).a;
    u32 family = (sig >> 8) & 0xf, model = (sig >> 4) & 0xf;
    C.stepping = sig & 0xf;
    C.family = family == 0xf ? family + ((sig >> 20) & 0xff) : family;
    C.model = family == 0x6 || family == 0xf ? (((sig >> 16) & 0xf) << 4) + model : model;
    C.brand[0] = 0;
    if (cpuid(0x80000000u, 0).a >= 0x80000004u) {
        for (u32 i = 0; i < 3; i++) {
            regs b = cpuid(0x80000002u + i, 0);
            memcpy(C.brand + i * 16, &b, 16);
        }
        C.brand[48] = 0;
    }
    // Brand strings are padded with leading spaces
    char *p = C.brand;
    while (*p == ' ') p++;
    memmove(C.brand, p, strlen(p) + 1);
}

static int is_amd(void) { return !strncmp(C.vendor, "AuthenticAMD", 12) || !strncmp(C.vendor, "HygonGenuine", 12); }

// Leaf 4 (Intel) and leaf 0x8000001D (AMD) share one register format
static void read_caches(void) {
    u32 leaf = 0;
    if (is_amd()) {
        if (cpuid(0x80000000u, 0).a >= 0x8000001du && (cpuid(0x80000001u, 0).c & (1u << 22))) leaf = 0x8000001du;
    } else if (cpuid(0, 0).a >= 4) {
        leaf = 4;
    }
    C.ncaches = 0;
    for (u32 sub = 0; leaf && sub < 16 && C.ncaches < MAX_CACHES; sub++) {
        regs r = cpuid(leaf, sub);
        u32 type = r.a & 0x1f;
        if (!type) break;
        cache_info *c = &C.caches[C.ncaches++];
        c->level = (r.a >> 5) & 7;
        c->type = type == 1 ? TYPE_DATA : type == 2 ? TYPE_CODE : TYPE_UNIFIED;
        c->full = (r.a >> 9) & 1;
        c->line = (r.b & 0xfff) + 1;
        c->ways = ((r.b >> 22) & 0x3ff) + 1;
        c->sets = r.c + 1;
        c->size = (u64)c->ways * (((r.b >> 12) & 0x3ff) + 1) * c->line * c->sets;
    }
}

// ==========================
// ===  TLB               ===
// ==========================

static void add_tlb(u32 level, const char *type, const char *pages, u32 entries, u32 ways, int full) {
    if (!entries || C.ntlbs >= MAX_TLBS) return;
    tlb_info *t = &C.tlbs[C.ntlbs++];
    t->level = level, t->type = type, t->pages = pages, t->entries = entries;
    t->full = full;
    t->ways = full ? entries : ways;
}

static void leaf2_add(void *context, u32 level, u32 type, const char *pages, u32 entries, u32 ways, int full) {
    (void)context;
    add_tlb(level, type == LEAF2_DATA ? TYPE_DATA : type == LEAF2_CODE ? TYPE_CODE : TYPE_UNIFIED, pages, entries,
            ways, full);
}

static void intel_leaf2_tlbs(void) {
    regs r = cpuid(2, 0);
    u32 words[4] = {r.a, r.b, r.c, r.d};
    leaf2_tlbs(words, leaf2_add, 0);
}

static void intel_leaf18_tlbs(void) {
    static const char *const PAGES[16] = {"",          "4 КБ",          "2 МБ",          "4 КБ/2 МБ",
                                          "4 МБ",      "4 КБ/4 МБ",     "2 МБ/4 МБ",     "4 КБ/2 МБ/4 МБ",
                                          "1 ГБ",      "4 КБ/1 ГБ",     "2 МБ/1 ГБ",     "4 КБ/2 МБ/1 ГБ",
                                          "4 МБ/1 ГБ", "4 КБ/4 МБ/1 ГБ", "2 МБ/4 МБ/1 ГБ", "4 КБ/2 МБ/4 МБ/1 ГБ"};
    u32 max_sub = cpuid(0x18, 0).a;
    for (u32 sub = 0; sub <= max_sub && sub < 64; sub++) {
        regs r = cpuid(0x18, sub);
        u32 type = r.d & 0x1f;
        if (!type) continue;
        // Types 4 and 5 (load and store only) are data TLBs too
        const char *name = type == 2 ? TYPE_CODE : type == 3 ? TYPE_UNIFIED : TYPE_DATA;
        u32 ways = r.b >> 16;
        add_tlb((r.d >> 5) & 7, name, PAGES[r.b & 0xf], ways * r.c, ways, (r.d >> 8) & 1);
    }
}

static u32 amd_l2_ways(u32 code) {
    static const u32 ways[16] = {0, 1, 2, 3, 4, 6, 8, 0, 16, 0, 32, 48, 64, 96, 128, 0xffffffff};
    return ways[code & 0xf];
}

static void amd_tlb(u32 level, const char *type, const char *pages, u32 entries, u32 ways) {
    int full = ways == 0xffffffff || ways == 0xff || ways == entries;
    add_tlb(level, type, pages, entries, full ? entries : ways, full);
}

static void amd_tlbs(void) {
    u32 max_ext = cpuid(0x80000000u, 0).a;
    if (max_ext >= 0x80000005u) {
        regs r = cpuid(0x80000005u, 0);
        amd_tlb(1, TYPE_DATA, "4 КБ", (r.b >> 16) & 0xff, r.b >> 24);
        amd_tlb(1, TYPE_CODE, "4 КБ", r.b & 0xff, (r.b >> 8) & 0xff);
        amd_tlb(1, TYPE_DATA, "2 МБ/4 МБ", (r.a >> 16) & 0xff, r.a >> 24);
        amd_tlb(1, TYPE_CODE, "2 МБ/4 МБ", r.a & 0xff, (r.a >> 8) & 0xff);
    }
    if (max_ext >= 0x80000006u) {
        regs r = cpuid(0x80000006u, 0);
        amd_tlb(2, TYPE_DATA, "4 КБ", (r.b >> 16) & 0xfff, amd_l2_ways(r.b >> 28));
        amd_tlb(2, TYPE_CODE, "4 КБ", r.b & 0xfff, amd_l2_ways((r.b >> 12) & 0xf));
        amd_tlb(2, TYPE_DATA, "2 МБ/4 МБ", (r.a >> 16) & 0xfff, amd_l2_ways(r.a >> 28));
        amd_tlb(2, TYPE_CODE, "2 МБ/4 МБ", r.a & 0xfff, amd_l2_ways((r.a >> 12) & 0xf));
    }
}

int cpuinfo_run(const exp_param *params) {
    (void)params;
    read_identity();
    read_caches();
    C.ntlbs = 0;
    if (is_amd()) {
        amd_tlbs();
    } else if (cpuid(0, 0).a >= 0x18) {
        intel_leaf18_tlbs();
    } else if (cpuid(0, 0).a >= 2) {
        intel_leaf2_tlbs();
    }
    C.memory_bytes = memory_total_bytes();
    C.tsc_ghz = tsc_per_ns;
    C.valid = 1;
    log_line("cpuinfo: %s, %d caches, %d TLBs", C.brand, C.ncaches, C.ntlbs);
    return 1;
}

// ==========================
// ===  Screen and text   ===
// ==========================

static void ways_text(char *out, usize size, u32 ways, int full) {
    if (full) {
        fmt(out, size, "полная");
    } else if (ways) {
        fmt(out, size, "%u", ways);
    } else {
        fmt(out, size, "—");
    }
}

void cpuinfo_draw(i32 height) {
    gfx_fill(0, 0, screen.width, height, COL_OUTER);
    if (!C.valid) return;
    i32 lh = gfx_line_height(), x = 48, y = 24;
    const char *title = "Идентификация процессора";
    gfx_text((screen.width - gfx_text_width(title)) / 2, y, title, COL_TEXT);
    y += lh + 20;
    char line[256], mem[32], ghz[16];
    format_bytes(mem, sizeof mem, (double)C.memory_bytes);
    format_number(ghz, sizeof ghz, C.tsc_ghz, 2);
    fmt(line, sizeof line, "Процессор: %s", C.brand[0] ? C.brand : C.vendor);
    gfx_text(x, y, line, COL_TEXT);
    y += lh + 6;
    fmt(line, sizeof line, "%s, семейство %u, модель %u, степпинг %u. Частота TSC %s ГГц. Память %s.", C.vendor,
        C.family, C.model, C.stepping, ghz, mem);
    gfx_text(x, y, line, COL_SUBTEXT);
    y += lh + 28;

    // Cells live in one buffer: up to 8 rows of 7 columns, 24 bytes each
    static char cache_text[MAX_CACHES][7][24];
    const char *cells[MAX_TLBS * 7];
    for (int i = 0; i < C.ncaches; i++) {
        cache_info *c = &C.caches[i];
        char(*t)[24] = cache_text[i];
        fmt(t[0], 24, "L%u", c->level);
        fmt(t[1], 24, "%s", c->type);
        format_bytes(t[2], 24, (double)c->size);
        fmt(t[3], 24, "%u Б", c->line);
        ways_text(t[4], 24, c->ways, c->full);
        format_count(t[5], 24, c->sets);
        if (c->full || !c->ways) fmt(t[6], 24, "—");
        else format_bytes(t[6], 24, (double)(c->size / c->ways));
        for (int k = 0; k < 7; k++) cells[i * 7 + k] = t[k];
    }
    gfx_text(x, y, "Кэш-память", COL_MAUVE);
    y += lh + 8;
    static const char *const cache_head[] = {"Уровень", "Тип", "Объём", "Линейка", "Ассоциативность", "Наборов",
                                             "Банк"};
    if (C.ncaches) {
        y += draw_table(x, y, cache_head, 7, cells, C.ncaches) + 20;
    } else {
        gfx_text(x, y, "Процессор не сообщает кэш-память. Возьмите её из документации процессора.", COL_PEACH);
        y += lh + 28;
    }

    static char tlb_text[MAX_TLBS][5][24];
    for (int i = 0; i < C.ntlbs; i++) {
        tlb_info *t = &C.tlbs[i];
        char(*s)[24] = tlb_text[i];
        fmt(s[0], 24, "L%u", t->level);
        fmt(s[1], 24, "%s", t->type);
        fmt(s[2], 24, "%s", t->pages);
        format_count(s[3], 24, t->entries);
        ways_text(s[4], 24, t->ways, t->full);
        for (int k = 0; k < 5; k++) cells[i * 5 + k] = s[k];
    }
    gfx_text(x, y, "Буферы TLB", COL_MAUVE);
    y += lh + 8;
    if (!C.ntlbs) {
        gfx_text(x, y, "Процессор не сообщает TLB. Возьмите их из документации процессора.", COL_PEACH);
        return;
    }
    static const char *const tlb_head[] = {"Уровень", "Тип", "Страницы", "Записей", "Ассоциативность"};
    draw_table(x, y, tlb_head, 5, cells, C.ntlbs);
}

static const char *type_en(const char *type) {
    return type == TYPE_DATA ? "data" : type == TYPE_CODE ? "instruction" : "unified";
}

usize cpuinfo_report(u8 *out, usize cap) {
    char *s = (char *)out;
    if (!C.valid) return fmt(s, cap, "error: identification has not run yet");
    usize n = fmt(s, cap, "experiment cpuinfo\nvendor %s\nbrand %s\nfamily %u\nmodel %u\nstepping %u\ntsc_ghz %.3f\n"
                          "memory_bytes %lu\n", C.vendor, C.brand, C.family, C.model, C.stepping, C.tsc_ghz,
                  C.memory_bytes);
    for (int i = 0; i < C.ncaches && n + 128 < cap; i++) {
        cache_info *c = &C.caches[i];
        n += fmt(s + n, cap - n, "cache L%u %s size %lu line %u ways %u sets %u full %d\n", c->level, type_en(c->type),
                 c->size, c->line, c->ways, c->sets, c->full);
    }
    for (int i = 0; i < C.ntlbs && n + 128 < cap; i++) {
        tlb_info *t = &C.tlbs[i];
        n += fmt(s + n, cap - n, "tlb L%u %s pages %s entries %u ways %u full %d\n", t->level, type_en(t->type), t->pages,
                 t->entries, t->ways, t->full);
    }
    return n;
}
