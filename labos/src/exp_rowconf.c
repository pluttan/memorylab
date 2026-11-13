#include "exp_rowconf.h"

#include "analysis.h"
#include "gfx.h"
#include "lib.h"
#include "plot.h"
#include "plotmath.h"

#define TPL_HIGH_LEVEL 31    // blocks every interrupt, so nothing lands inside a measurement
#define LINES_PER_SLICE 512  // the firmware timers get a turn between slices
#define MAX_ROUNDS 63
#define ALIGN (2ull << 20)
#define MB (1ull << 20)

typedef struct {
    u64 base;  // physical address of the block
    u32 lines;
    u32 ref2;  // index of the second reference line
    double *ns, *ns2;  // pair time with the reference and with the second reference, ns
    u8 *flags;         // 2 * lines bytes: conflicts with each reference
    double threshold, separation;
    bank_estimate estimate;
    u64 memory_bytes;
    u64 pages;  // DRAM pages (rows of a rank) in the whole memory
    u64 duration_us;
    int rounds;
    int valid;
} rowconf_result;

static rowconf_result R;
static usize result_pages;

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

static void measure_pass(const u8 *block, u32 lines, u32 ref, double *ns, int rounds) {
    const volatile u64 *a = (const volatile u64 *)(block + (usize)ref * ROWCONF_LINE);
    for (u32 first = 0; first < lines; first += LINES_PER_SLICE) {
        usize old = BS->raise_tpl(TPL_HIGH_LEVEL);
        for (u32 i = first; i < first + LINES_PER_SLICE && i < lines; i++) {
            const volatile u64 *b = (const volatile u64 *)(block + (usize)i * ROWCONF_LINE);
            ns[i] = i == ref ? 0 : pair_ticks(a, b, rounds) / tsc_per_ns;
        }
        BS->restore_tpl(old);
    }
}

// The arrays of the previous result go; one allocation holds both time arrays and the flags
static int prepare_result(u32 lines) {
    if (R.ns) BS->free_pages((efi_phys_addr)R.ns, result_pages);
    R.ns = 0;
    R.valid = 0;
    result_pages = ((usize)lines * (2 * sizeof(double) + 2) + 4095) / 4096;
    u8 *mem = alloc_pages(result_pages);
    if (!mem) return 0;
    R.lines = lines;
    R.ns = (double *)mem;
    R.ns2 = R.ns + lines;
    R.flags = (u8 *)(R.ns2 + lines);
    return 1;
}

int rowconf_run(const exp_param *params) {
    u64 block_bytes = (u64)params[0].value * MB;
    int rounds = (int)params[1].value;
    if (rounds < 1) rounds = 1;
    if (rounds > MAX_ROUNDS) rounds = MAX_ROUNDS;
    u32 lines = (u32)(block_bytes / ROWCONF_LINE);
    if (!prepare_result(lines)) return 0;
    // One extra 2 MB so that the block can start on a 2 MB boundary
    usize block_pages = (block_bytes + ALIGN) / 4096;
    u8 *raw = alloc_pages(block_pages);
    if (!raw) return 0;
    u8 *block = (u8 *)(((u64)raw + ALIGN - 1) & ~(ALIGN - 1));
    memset(block, 1, block_bytes);
    R.base = (u64)block;
    R.rounds = rounds;
    u64 start = now_us();
    log_line("rowconf: block %lu MB at %lx, %d rounds per line", block_bytes / MB, R.base, rounds);

    measure_pass(block, lines, 0, R.ns, rounds);
    R.threshold = split_threshold(R.ns + 1, lines - 1, &R.separation);
    R.ref2 = 0;
    for (u32 i = 1; i < lines && !R.ref2; i++) {
        if (R.ns[i] > R.threshold) R.ref2 = i;
    }
    memset(&R.estimate, 0, sizeof R.estimate);
    if (R.ref2) {
        measure_pass(block, lines, R.ref2, R.ns2, rounds);
        u8 *a = R.flags, *b = R.flags + lines;
        for (u32 i = 0; i < lines; i++) {
            a[i] = i != 0 && R.ns[i] > R.threshold;
            b[i] = i != R.ref2 && R.ns2[i] > R.threshold;
        }
        R.estimate = estimate_banks(a, b, lines, ROWCONF_LINE);
    } else {
        memset(R.ns2, 0, lines * sizeof(double));
    }
    R.memory_bytes = memory_total_bytes();
    R.pages = R.estimate.row_bytes ? R.memory_bytes / R.estimate.row_bytes : 0;
    R.duration_us = now_us() - start;
    R.valid = 1;
    BS->free_pages((efi_phys_addr)raw, block_pages);
    log_line("rowconf: threshold %.1f ns, separation %.2f, banks %u, row %u B, %lu us", R.threshold, R.separation,
             R.estimate.banks, R.estimate.row_bytes, R.duration_us);
    return 1;
}

// Without two clear groups of times the estimate means nothing (QEMU, or no access to DRAM timing).
// Real modules give a gap of 30-60 %, at most a few hundred banks and rows of 1-16 KB
static int has_conflicts(void) {
    return R.ref2 && R.separation > 0.15 && R.separation < 5 && R.estimate.banks >= 2 && R.estimate.banks <= 512 &&
           R.estimate.row_bytes >= 1024;
}

static void draw_conclusions(const plot *p, i32 y) {
    i32 lh = gfx_line_height();
    if (!has_conflicts()) {
        gfx_text(p->fx, y, "Конфликтов строк не видно: времена не разделились на две полосы.", COL_RED);
        gfx_text(p->fx, y + lh + 4, "Так бывает в виртуальной машине: у неё нет настоящей DRAM.", COL_SUBTEXT);
        return;
    }
    char line[256], mem[32], row[32], pages[32], same[32], lines[32];
    format_bytes(mem, sizeof mem, (double)R.memory_bytes);
    format_bytes(row, sizeof row, R.estimate.row_bytes);
    format_count(pages, sizeof pages, R.pages);
    format_count(same, sizeof same, R.estimate.same_bank);
    format_count(lines, sizeof lines, R.estimate.lines);
    fmt(line, sizeof line, "Банков: %u   Строка (страница): %s   Страниц: %s   Память: %s", R.estimate.banks, row,
        pages, mem);
    gfx_text(p->fx, y, line, COL_TEXT);
    fmt(line, sizeof line, "Порог %.0f нс, полосы различаются на %.0f%%. Линеек одного банка: %s из %s.", R.threshold,
        R.separation * 100, same, lines);
    gfx_text(p->fx, y + lh + 4, line, COL_SUBTEXT);
    fmt(line, sizeof line, "Блок с физического адреса %lx, %d повторов на пару, замер %lu мс.", R.base, R.rounds,
        R.duration_us / 1000);
    gfx_text(p->fx, y + 2 * (lh + 4), line, COL_SUBTEXT);
}

void rowconf_draw(i32 height) {
    gfx_fill(0, 0, screen.width, height, COL_OUTER);
    if (!R.valid) return;
    // x values and the conflicting points share one scratch allocation
    usize scratch_pages = ((usize)R.lines * 3 * sizeof(double) + 4095) / 4096;
    double *xs = alloc_pages(scratch_pages);
    if (!xs) return;
    double *hi_x = xs + R.lines, *hi_y = hi_x + R.lines;
    double mean = 0;
    for (u32 i = 1; i < R.lines; i++) mean += R.ns[i];
    mean /= R.lines - 1;
    double top = has_conflicts() ? R.threshold * 1.8 : mean * 2;
    usize n_hi = 0;
    for (u32 i = 0; i < R.lines; i++) {
        xs[i] = (double)i * ROWCONF_LINE;
        if (i && R.ns[i] > R.threshold) hi_x[n_hi] = xs[i], hi_y[n_hi++] = R.ns[i] < top ? R.ns[i] : top;
    }
    i32 lh = gfx_line_height();
    plot p = {.x = 0, .y = 0, .w = screen.width, .h = height - (lh + 4) * 3 - 12, .x0 = 0,
              .x1 = (double)R.lines * ROWCONF_LINE, .y0 = 0, .y1 = top, .x_kind = AXIS_BYTES,
              .title = "Опыт 1. Конфликты строк DRAM: время пары чтений",
              .x_label = "Расстояние от опорной линейки", .y_label = "нс", .legend_row = 1};
    plot_frame(&p);
    plot_dots(&p, xs + 1, R.ns + 1, R.lines - 1, COL_BLUE);
    plot_dots(&p, hi_x, hi_y, n_hi, COL_RED);
    plot_hline(&p, R.threshold, COL_PEACH);
    legend_item items[] = {{"другой банк или та же строка", COL_BLUE}, {"конфликт строк", COL_RED},
                           {"порог", COL_PEACH}};
    plot_legend(&p, items, 3);
    BS->free_pages((efi_phys_addr)xs, scratch_pages);
    draw_conclusions(&p, p.y + p.h + 8);
}

usize rowconf_report(u8 *out, usize cap) {
    char *s = (char *)out;
    if (!R.valid) return fmt(s, cap, "error: experiment 1 has not run yet");
    usize n = fmt(s, cap,
                  "experiment rowconf\nbase %lx\nblock_bytes %lu\nrounds %d\nthreshold_ns %.2f\nseparation %.3f\n"
                  "conflicts %s\nbanks %u\nrow_bytes %u\nsame_bank_lines %lu\nlines %lu\nmemory_bytes %lu\n"
                  "pages %lu\nduration_us %lu\nref2_offset %u\ndata offset ns ns2\n",
                  R.base, (u64)R.lines * ROWCONF_LINE, R.rounds, R.threshold, R.separation,
                  has_conflicts() ? "yes" : "no", R.estimate.banks, R.estimate.row_bytes, R.estimate.same_bank,
                  R.estimate.lines, R.memory_bytes, R.pages, R.duration_us, R.ref2 * ROWCONF_LINE);
    for (u32 i = 0; i < R.lines && n + 48 < cap; i++) {
        n += fmt(s + n, cap - n, "%u %.1f %.1f\n", i * ROWCONF_LINE, R.ns[i], R.ns2[i]);
    }
    return n;
}
