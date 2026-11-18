#include "exp_rowconf.h"

#include "analysis.h"
#include "gfx.h"
#include "lib.h"
#include "plot.h"
#include "plotmath.h"
#include "widgets.h"

#define TPL_HIGH_LEVEL 31    // blocks every interrupt, so nothing lands inside a measurement
#define LINES_PER_SLICE 512  // the firmware timers get a turn between slices
#define MAX_ROUNDS 63
#define RETRIES 2            // extra measurements of the lines above the low band
#define ALIGN (2ull << 20)
#define MB (1ull << 20)
#define HIST_BINS 120

typedef struct {
    double *ns;  // final pair time with the reference of the pass, ns
    time_modes modes;
    usize retried;  // lines measured again
} pass_result;

static struct {
    u64 base;  // physical address of the block
    u32 lines;
    u32 ref2;  // index of the second reference line
    pass_result a, b;
    u8 *flags;  // 2 * lines bytes: conflicts with each reference
    bank_estimate estimate;
    u64 memory_bytes;
    u64 pages;  // DRAM pages (rows of a rank) in the whole memory
    u64 duration_us;
    int rounds;
    int valid;
} R;

static usize result_pages;

#ifdef ROWCONF_SYNTHETIC
// Drawing check in QEMU, which has no DRAM timing: one channel, 16 banks with Skylake style XOR, 8 KB rows,
// and the noise of real hardware: a random tail and rare bursts that only add time
static u32 model_bank(u64 a) {
    return (u32)((((a >> 6) ^ (a >> 13)) & 1) | (((a >> 14) ^ (a >> 18)) & 1) << 1 | (((a >> 15) ^ (a >> 19)) & 1) << 2 |
                 (((a >> 16) ^ (a >> 20)) & 1) << 3);
}
static u32 pair_ticks(const volatile u64 *a, const volatile u64 *b, int rounds) {
    (void)rounds;
    u64 x = (u64)a, y = (u64)b, noise = rdtsc() * 2654435761u;
    u32 t = (model_bank(x) == model_bank(y) && (x >> 17) != (y >> 17) ? 340 : 214) + (u32)(noise >> 7) % 12;
    u32 r = (u32)(noise >> 20) % 100;
    return r < 2 ? t + 200 : r < 12 ? t + 20 + (u32)(noise >> 33) % 80 : t;
}
#else
static inline void clflush(const volatile void *p) { __asm__ volatile("clflush (%0)" ::"r"(p) : "memory"); }
static inline void mfence(void) { __asm__ volatile("mfence" ::: "memory"); }
static inline void lfence(void) { __asm__ volatile("lfence" ::: "memory"); }

// Both lines leave the caches, then both are read at once: the reads overlap unless the bank has to
// switch rows between them. The median of the rounds is kept
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

static double pair_ns(const u8 *block, u32 ref, u32 i, int rounds) {
    const volatile u64 *a = (const volatile u64 *)(block + (usize)ref * ROWCONF_LINE);
    const volatile u64 *b = (const volatile u64 *)(block + (usize)i * ROWCONF_LINE);
    return pair_ticks(a, b, rounds) / tsc_per_ns;
}

// One reference: a pass over every line, then the lines above the low band measured again and the
// smaller time kept (interference only adds time), then the two peaks found on the final times
static void measure_pass(const u8 *block, u32 ref, pass_result *out, int rounds) {
    u32 lines = R.lines;
    for (u32 first = 0; first < lines; first += LINES_PER_SLICE) {
        usize old = BS->raise_tpl(TPL_HIGH_LEVEL);
        for (u32 i = first; i < first + LINES_PER_SLICE && i < lines; i++) {
            out->ns[i] = i == ref ? 0 : pair_ns(block, ref, i, rounds);
        }
        BS->restore_tpl(old);
    }
    time_modes first = find_modes(out->ns, lines);
    out->retried = 0;
    for (int pass = 0; pass < RETRIES; pass++) {
        usize old = BS->raise_tpl(TPL_HIGH_LEVEL);
        for (u32 i = 0; i < lines; i++) {
            if (i == ref || out->ns[i] <= first.low * RETRY_ABOVE_LOW) continue;
            double again = pair_ns(block, ref, i, rounds);
            if (again < out->ns[i]) out->ns[i] = again;
            out->retried += pass == 0;
            if ((i & 1023) == 1023) BS->restore_tpl(old), old = BS->raise_tpl(TPL_HIGH_LEVEL);
        }
        BS->restore_tpl(old);
    }
    out->modes = find_modes(out->ns, lines);
}

// The arrays of the previous result go; one allocation holds both time arrays and the flags
static int prepare_result(u32 lines) {
    if (R.a.ns) BS->free_pages((efi_phys_addr)R.a.ns, result_pages);
    R.a.ns = R.b.ns = 0;
    R.valid = 0;
    result_pages = ((usize)lines * (2 * sizeof(double) + 2) + 4095) / 4096;
    u8 *mem = alloc_pages(result_pages);
    if (!mem) return 0;
    R.lines = lines;
    R.a.ns = (double *)mem;
    R.b.ns = R.a.ns + lines;
    R.flags = (u8 *)(R.b.ns + lines);
    return 1;
}

// The second reference: a line of the conflict peak, as near its centre as possible
static u32 pick_ref2(void) {
    u32 best = 0;
    double best_d = 1e300, high = R.a.modes.high;
    for (u32 i = 1; i < R.lines && high > 0; i++) {
        double d = R.a.ns[i] > high ? R.a.ns[i] - high : high - R.a.ns[i];
        if (R.a.ns[i] > R.a.modes.threshold && d < best_d) best_d = d, best = i;
    }
    return best;
}

int rowconf_run(const exp_param *params) {
    u64 block_bytes = (u64)params[0].value * MB;
    int rounds = (int)params[1].value;
    if (rounds < 1) rounds = 1;
    if (rounds > MAX_ROUNDS) rounds = MAX_ROUNDS;
    if (!prepare_result((u32)(block_bytes / ROWCONF_LINE))) return 0;
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

    measure_pass(block, 0, &R.a, rounds);
    R.ref2 = pick_ref2();
    memset(&R.estimate, 0, sizeof R.estimate);
    memset(&R.b.modes, 0, sizeof R.b.modes);
    u8 *fa = R.flags, *fb = R.flags + R.lines;
    if (R.ref2) {
        measure_pass(block, R.ref2, &R.b, rounds);
        for (u32 i = 0; i < R.lines; i++) {
            fa[i] = i != 0 && R.a.ns[i] > R.a.modes.threshold;
            fb[i] = i != R.ref2 && R.b.modes.high > 0 && R.b.ns[i] > R.b.modes.threshold;
        }
        R.estimate = estimate_banks(fa, fb, R.lines, ROWCONF_LINE);
    } else {
        memset(R.b.ns, 0, R.lines * sizeof(double));
    }
    R.memory_bytes = memory_total_bytes();
    R.pages = R.estimate.row_bytes ? R.memory_bytes / R.estimate.row_bytes : 0;
    R.duration_us = now_us() - start;
    R.valid = 1;
    BS->free_pages((efi_phys_addr)raw, block_pages);
    log_line("rowconf: low %.1f high %.1f threshold %.1f contrast %.1f peak %lu retried %lu; ref2 %u; "
             "banks %u row %u B, %lu us",
             R.a.modes.low, R.a.modes.high, R.a.modes.threshold, R.a.modes.contrast, R.a.modes.high_count,
             R.a.retried, R.ref2 * ROWCONF_LINE, R.estimate.banks, R.estimate.row_bytes, R.duration_us);
    return 1;
}

// ==========================
// ===  Verdict           ===
// ==========================

typedef enum { OK, NO_TIMES, NO_PEAK, NO_SECOND_PEAK, ODD_NUMBERS } verdict;

static verdict judge(void) {
    if (R.a.modes.low < 1) return NO_TIMES;
    if (R.a.modes.high <= 0 || !R.ref2) return NO_PEAK;
    if (R.b.modes.high <= 0) return NO_SECOND_PEAK;
    if (R.estimate.banks < 2 || R.estimate.banks > 512 || R.estimate.row_bytes < 1024) return ODD_NUMBERS;
    return OK;
}

static const char *const VERDICT_TEXT[] = {
    [OK] = "ok",
    [NO_TIMES] = "no_times",
    [NO_PEAK] = "no_peak",
    [NO_SECOND_PEAK] = "no_second_peak",
    [ODD_NUMBERS] = "odd_numbers",
};

static void draw_verdict(i32 x, i32 y, i32 width) {
    char line[320], mem[32], row[32], pages[32], same[32], lines[32];
    i32 lh = gfx_line_height() + 6;
    const time_modes *m = &R.a.modes;
    switch (judge()) {
        case OK:
            format_bytes(mem, sizeof mem, (double)R.memory_bytes);
            format_bytes(row, sizeof row, R.estimate.row_bytes);
            format_count(pages, sizeof pages, R.pages);
            format_count(same, sizeof same, R.estimate.same_bank);
            format_count(lines, sizeof lines, R.estimate.lines);
            fmt(line, sizeof line, "Банков: %u     Строка (страница): %s     Страниц: %s     Память: %s",
                R.estimate.banks, row, pages, mem);
            gfx_text(x, y, line, COL_TEXT);
            fmt(line, sizeof line,
                "Без конфликта пара читается за %.0f нс, с конфликтом строк за %.0f нс. В банке опорной линейки "
                "%s линеек из %s.",
                m->low, m->high, same, lines);
            wrap_text(x, y + lh, width, line, COL_SUBTEXT);
            return;
        case NO_TIMES:
            gfx_text(x, y, "Времена почти нулевые: у этой машины нет настоящей DRAM (виртуальная машина).", COL_RED);
            return;
        case NO_PEAK:
            fmt(line, sizeof line,
                "Пик конфликтов строк не найден. Основная полоса %.0f нс, выше неё нет отдельной группы времён: "
                "контраст %.1f, нужно не меньше 4. Увеличьте число повторов или размер блока.",
                m->low, m->contrast);
            break;
        case NO_SECOND_PEAK:
            fmt(line, sizeof line,
                "Пик конфликтов найден (%.0f нс, %lu линеек), но второй проход от линейки %u его не повторил. "
                "Запустите опыт ещё раз.",
                m->high, m->high_count, R.ref2 * ROWCONF_LINE);
            break;
        case ODD_NUMBERS:
            format_bytes(row, sizeof row, R.estimate.row_bytes);
            fmt(line, sizeof line,
                "Пик конфликтов найден (%.0f нс), но числа не сходятся: банков %u, строка %s. Банков должно "
                "быть от 2 до 512, строка от 1 КБ. Запустите опыт ещё раз.",
                m->high, R.estimate.banks, row);
            break;
    }
    wrap_text(x, y, width, line, COL_PEACH);
}

// ==========================
// ===  Screen            ===
// ==========================

static void draw_dots(i32 x, i32 y, i32 w, i32 h) {
    double top = R.a.modes.high > 0 ? R.a.modes.high * 1.35 : R.a.modes.low * 2;
    if (top <= 0) top = 1;
    usize scratch_pages = ((usize)R.lines * 2 * sizeof(double) + 4095) / 4096;
    double *xs = alloc_pages(scratch_pages);
    if (!xs) return;
    double *ys = xs + R.lines;
    plot p = {.x = x, .y = y, .w = w, .h = h, .x0 = 0, .x1 = (double)R.lines * ROWCONF_LINE, .y0 = 0, .y1 = top,
              .x_kind = AXIS_BYTES, .title = "Время пары чтений: опорная линейка и каждая линейка блока",
              .x_label = "Расстояние от опорной линейки", .y_label = "нс", .legend_row = 1};
    plot_frame(&p);
    // Low band first, conflicts over it; times above the axis sit on its top edge
    for (int pass = 0; pass < 2; pass++) {
        usize n = 0;
        for (u32 i = 1; i < R.lines; i++) {
            int conflict = R.a.modes.high > 0 && R.a.ns[i] > R.a.modes.threshold;
            if (conflict != pass) continue;
            xs[n] = (double)i * ROWCONF_LINE;
            ys[n++] = R.a.ns[i] < top ? R.a.ns[i] : top;
        }
        plot_dots(&p, xs, ys, n, pass ? COL_RED : COL_BLUE, pass ? 1.6 : 0.9);
    }
    if (R.a.modes.high > 0) plot_hline(&p, R.a.modes.threshold, COL_PEACH);
    legend_item items[] = {{"другой банк или та же строка", COL_BLUE}, {"конфликт строк", COL_RED},
                           {"порог", COL_PEACH}};
    plot_legend(&p, items, R.a.modes.high > 0 ? 3 : 1);
    BS->free_pages((efi_phys_addr)xs, scratch_pages);
}

static void draw_histogram(i32 x, i32 y, i32 w, i32 h) {
    const time_modes *m = &R.a.modes;
    double lo = m->low * 0.8, hi = (m->high > 0 ? m->high : m->low * 1.5) * 1.3;
    if (hi <= lo) hi = lo + 1;
    static u32 bins[HIST_BINS];
    static double left[HIST_BINS], low_counts[HIST_BINS], high_counts[HIST_BINS];
    histogram(R.a.ns + 1, R.lines - 1, lo, hi, bins, HIST_BINS);
    double width = (hi - lo) / HIST_BINS, top = 1;
    for (int b = 0; b < HIST_BINS; b++) {
        left[b] = lo + b * width;
        int above = m->high > 0 && left[b] + width / 2 > m->threshold;
        low_counts[b] = above ? 0 : bins[b];
        high_counts[b] = above ? bins[b] : 0;
        if (bins[b] > top) top = bins[b];
    }
    plot p = {.x = x, .y = y, .w = w, .h = h, .x0 = lo, .x1 = hi, .y0 = 0.8, .y1 = top * 2, .log_y = 1,
              .title = "Сколько линеек с каждым временем", .x_label = "Время пары, нс",
              .y_label = "линеек", .legend_row = 1};
    plot_frame(&p);
    plot_bars(&p, left, width, low_counts, HIST_BINS, COL_BLUE);
    plot_bars(&p, left, width, high_counts, HIST_BINS, COL_RED);
    if (m->high > 0) plot_vline(&p, m->threshold, COL_PEACH);
    legend_item items[] = {{"без конфликта", COL_BLUE}, {"конфликт строк", COL_RED}};
    plot_legend(&p, items, m->high > 0 ? 2 : 1);
}

void rowconf_draw(i32 height) {
    gfx_fill(0, 0, screen.width, height, COL_OUTER);
    if (!R.valid) return;
    i32 lh = gfx_line_height(), text_h = 3 * (lh + 6) + 16;
    i32 graph_h = height - text_h, split = (i32)screen.width * 58 / 100;
    draw_dots(0, 0, split, graph_h);
    draw_histogram(split, 0, (i32)screen.width - split, graph_h);
    draw_verdict(48, graph_h + 8, (i32)screen.width - 96);
}

usize rowconf_report(u8 *out, usize cap) {
    char *s = (char *)out;
    if (!R.valid) return fmt(s, cap, "error: experiment 1 has not run yet");
    const time_modes *a = &R.a.modes, *b = &R.b.modes;
    usize n = fmt(s, cap,
                  "experiment rowconf\nverdict %s\nbase %lx\nblock_bytes %lu\nrounds %d\n"
                  "low_ns %.2f\nhigh_ns %.2f\nthreshold_ns %.2f\ncontrast %.2f\npeak_lines %lu\nretried %lu\n"
                  "ref2_offset %u\nref2_low_ns %.2f\nref2_high_ns %.2f\nref2_contrast %.2f\n"
                  "banks %u\nrow_bytes %u\nsame_bank_lines %lu\nrow_lines %lu\nlines %lu\n"
                  "memory_bytes %lu\npages %lu\nduration_us %lu\ntsc_ghz %.4f\ndata offset ns ns2\n",
                  VERDICT_TEXT[judge()], R.base, (u64)R.lines * ROWCONF_LINE, R.rounds, a->low, a->high, a->threshold,
                  a->contrast, a->high_count, R.a.retried, R.ref2 * ROWCONF_LINE, b->low, b->high, b->contrast,
                  R.estimate.banks, R.estimate.row_bytes, R.estimate.same_bank, R.estimate.row_lines,
                  R.estimate.lines, R.memory_bytes, R.pages, R.duration_us, tsc_per_ns);
    for (u32 i = 0; i < R.lines && n + 48 < cap; i++) {
        n += fmt(s + n, cap - n, "%u %.1f %.1f\n", i * ROWCONF_LINE, R.a.ns[i], R.b.ns[i]);
    }
    return n;
}
