#include "exp_rowconf.h"

#include "gfx.h"
#include "lib.h"
#include "plot.h"
#include "plotmath.h"

#define TPL_HIGH_LEVEL 31  // blocks every interrupt, so nothing lands inside a measurement
#define LINES_PER_SLICE 512  // the firmware timers get a turn between slices
#define MAX_ROUNDS 63

#ifdef ROWCONF_SYNTHETIC
// Drawing check in QEMU, which has no DRAM timing: one channel, 16 banks with Skylake style XOR, 8 KB rows
static u32 model_bank(u64 a) {
    return (u32)((((a >> 6) ^ (a >> 13)) & 1) | (((a >> 14) ^ (a >> 18)) & 1) << 1 | (((a >> 15) ^ (a >> 19)) & 1) << 2 |
                 (((a >> 16) ^ (a >> 20)) & 1) << 3);
}
#define pair_ticks(a, b, rounds)                                                                      \
    ((u32)((model_bank((u64)(a)) == model_bank((u64)(b)) && ((u64)(a) >> 17) != ((u64)(b) >> 17) ? 340 : 220) + \
           (rdtsc() & 15)) * (rounds > 0))
#else
static inline void clflush(const volatile void *p) { __asm__ volatile("clflush (%0)" ::"r"(p) : "memory"); }
static inline void mfence(void) { __asm__ volatile("mfence" ::: "memory"); }
static inline void lfence(void) { __asm__ volatile("lfence" ::: "memory"); }

// Both lines leave the caches, then both are read at once: the reads overlap unless the bank has to
// switch rows between them
static u32 pair_ticks(const volatile u64 *a, const volatile u64 *b, int rounds) {
    u32 t[MAX_ROUNDS];
    for (int r = 0; r < rounds; r++) {
        clflush(a);
        clflush(b);
        mfence();
        lfence();
        u64 t0 = rdtsc();
        lfence();
        (void)*a;
        (void)*b;
        lfence();
        t[r] = (u32)(rdtsc() - t0);
    }
    for (int i = 1; i < rounds; i++) {
        u32 v = t[i];
        int j = i;
        for (; j > 0 && t[j - 1] > v; j--) t[j] = t[j - 1];
        t[j] = v;
    }
    return t[rounds / 2];
}
#endif

static void measure_pass(const u8 *block, u32 ref, double *ns, int rounds) {
    const volatile u64 *a = (const volatile u64 *)(block + (usize)ref * ROWCONF_LINE);
    for (u32 first = 0; first < ROWCONF_LINES; first += LINES_PER_SLICE) {
        usize old = BS->raise_tpl(TPL_HIGH_LEVEL);
        for (u32 i = first; i < first + LINES_PER_SLICE; i++) {
            const volatile u64 *b = (const volatile u64 *)(block + (usize)i * ROWCONF_LINE);
            ns[i] = i == ref ? 0 : pair_ticks(a, b, rounds) / tsc_per_ns;
        }
        BS->restore_tpl(old);
    }
}

int rowconf_run(rowconf_result *r, int rounds) {
    if (rounds < 1) rounds = 1;
    if (rounds > MAX_ROUNDS) rounds = MAX_ROUNDS;
    // One extra 2 MB so that the block can start on a 2 MB boundary
    u8 *raw = alloc_pages(2 * ROWCONF_BLOCK / 4096);
    if (!raw) return 0;
    u8 *block = (u8 *)(((u64)raw + ROWCONF_BLOCK - 1) & ~(u64)(ROWCONF_BLOCK - 1));
    memset(block, 1, ROWCONF_BLOCK);
    r->base = (u64)block;
    u64 start = now_us();
    log_line("rowconf: block at %lx, %d rounds per line", r->base, rounds);

    measure_pass(block, 0, r->ns, rounds);
    r->threshold = split_threshold(r->ns + 1, ROWCONF_LINES - 1, &r->separation);
    r->ref2 = 0;
    for (u32 i = 1; i < ROWCONF_LINES && !r->ref2; i++) {
        if (r->ns[i] > r->threshold) r->ref2 = i;
    }
    memset(&r->estimate, 0, sizeof r->estimate);
    if (r->ref2) {
        measure_pass(block, r->ref2, r->ns2, rounds);
        static u8 a[ROWCONF_LINES], b[ROWCONF_LINES];
        for (u32 i = 0; i < ROWCONF_LINES; i++) {
            a[i] = i != 0 && r->ns[i] > r->threshold;
            b[i] = i != r->ref2 && r->ns2[i] > r->threshold;
        }
        r->estimate = estimate_banks(a, b, ROWCONF_LINES, ROWCONF_LINE);
    } else {
        memset(r->ns2, 0, sizeof r->ns2);
    }
    r->memory_bytes = memory_total_bytes();
    r->pages = r->estimate.row_bytes ? r->memory_bytes / r->estimate.row_bytes : 0;
    r->duration_us = now_us() - start;
    BS->free_pages((efi_phys_addr)raw, 2 * ROWCONF_BLOCK / 4096);
    log_line("rowconf: threshold %.1f ns, separation %.2f, banks %u, row %u B, %lu us", r->threshold, r->separation,
             r->estimate.banks, r->estimate.row_bytes, r->duration_us);
    return 1;
}

// Without two clear groups of times the estimate means nothing (QEMU, or no access to DRAM timing).
// Real modules give a gap of 30-60 %, at most a few hundred banks and rows of 1-16 KB
static int has_conflicts(const rowconf_result *r) {
    return r->ref2 && r->separation > 0.15 && r->separation < 5 && r->estimate.banks >= 2 &&
           r->estimate.banks <= 512 && r->estimate.row_bytes >= 1024;
}

void rowconf_draw(const rowconf_result *r) {
    static double xs[ROWCONF_LINES], hi_x[ROWCONF_LINES], hi_y[ROWCONF_LINES];
    double mean = 0;
    for (u32 i = 1; i < ROWCONF_LINES; i++) mean += r->ns[i];
    mean /= ROWCONF_LINES - 1;
    double top = has_conflicts(r) ? r->threshold * 1.8 : mean * 2;
    usize n_hi = 0;
    for (u32 i = 0; i < ROWCONF_LINES; i++) {
        xs[i] = (double)i * ROWCONF_LINE;
        if (r->ns[i] > r->threshold && i) hi_x[n_hi] = xs[i], hi_y[n_hi++] = r->ns[i] < top ? r->ns[i] : top;
    }
    i32 lh = gfx_line_height();
    i32 text_h = lh * 3 + 24;
    gfx_fill(0, 0, screen.width, screen.height, COL_OUTER);
    plot p = {.x = 0, .y = 0, .w = screen.width, .h = screen.height - text_h - lh - 8, .x0 = 0,
              .x1 = ROWCONF_BLOCK, .y0 = 0, .y1 = top, .x_kind = AXIS_BYTES,
              .title = "Опыт 1. Конфликты строк DRAM: время пары чтений",
              .x_label = "Расстояние от опорной линейки", .y_label = "нс", .legend_row = 1};
    plot_frame(&p);
    plot_dots(&p, xs + 1, r->ns + 1, ROWCONF_LINES - 1, COL_BLUE);
    plot_dots(&p, hi_x, hi_y, n_hi, COL_RED);
    plot_hline(&p, r->threshold, COL_PEACH);
    legend_item items[] = {{"другой банк или та же строка", COL_BLUE}, {"конфликт строк", COL_RED},
                           {"порог", COL_PEACH}};
    plot_legend(&p, items, 3);

    char line[256], mem[32], row[32];
    i32 y = p.y + p.h + 8;
    if (!has_conflicts(r)) {
        gfx_text(p.fx, y, "Конфликтов строк не видно: времена не разделились на две полосы.", COL_RED);
        gfx_text(p.fx, y + lh + 4, "Так бывает в виртуальной машине: у неё нет настоящей DRAM.", COL_SUBTEXT);
        return;
    }
    format_bytes(mem, sizeof mem, (double)r->memory_bytes);
    format_bytes(row, sizeof row, r->estimate.row_bytes);
    char pages[32], same[32], lines[32];
    format_count(pages, sizeof pages, r->pages);
    format_count(same, sizeof same, r->estimate.same_bank);
    format_count(lines, sizeof lines, r->estimate.lines);
    fmt(line, sizeof line, "Банков: %u   Строка (страница): %s   Страниц: %s   Память: %s", r->estimate.banks, row,
        pages, mem);
    gfx_text(p.fx, y, line, COL_TEXT);
    fmt(line, sizeof line, "Порог %.0f нс, полосы различаются на %.0f%%. Линеек одного банка: %s из %s.",
        r->threshold, r->separation * 100, same, lines);
    gfx_text(p.fx, y + lh + 4, line, COL_SUBTEXT);
    fmt(line, sizeof line, "Блок с физического адреса %lx, замер %lu мс.", r->base, r->duration_us / 1000);
    gfx_text(p.fx, y + 2 * (lh + 4), line, COL_SUBTEXT);
}

usize rowconf_report(const rowconf_result *r, u8 *out, usize cap) {
    char *s = (char *)out;
    usize n = fmt(s, cap,
                  "experiment rowconf\nbase %lx\nthreshold_ns %.2f\nseparation %.3f\nconflicts %s\nbanks %u\n"
                  "row_bytes %u\nsame_bank_lines %lu\nlines %lu\nmemory_bytes %lu\npages %lu\nduration_us %lu\n"
                  "ref2_offset %u\ndata offset ns ns2\n",
                  r->base, r->threshold, r->separation, has_conflicts(r) ? "yes" : "no", r->estimate.banks,
                  r->estimate.row_bytes, r->estimate.same_bank, r->estimate.lines, r->memory_bytes, r->pages,
                  r->duration_us, r->ref2 * ROWCONF_LINE);
    for (u32 i = 0; i < ROWCONF_LINES && n + 48 < cap; i++) {
        n += fmt(s + n, cap - n, "%u %.1f %.1f\n", i * ROWCONF_LINE, r->ns[i], r->ns2[i]);
    }
    return n;
}
