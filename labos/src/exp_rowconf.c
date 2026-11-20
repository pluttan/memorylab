#include "exp_rowconf.h"

#include "analysis.h"
#include "gfx.h"
#include "hwprefetch.h"
#include "lib.h"
#include "measure.h"
#include "plot.h"
#include "plotmath.h"
#include "textbook.h"
#include "widgets.h"

#define TPL_HIGH_LEVEL 31    // blocks every interrupt, so nothing lands inside a measurement
#define LINES_PER_SLICE 512  // the firmware timers get a turn between slices
#define MAX_ROUNDS 63
#define RETRIES 2            // extra measurements of the lines above the low band
#define MB (1ull << 20)
#define HIST_BINS 120
#define CLASSIC_POINTS 16384  // distances of the classic graph
#define CLASSIC_REPEATS 3     // the smallest time of these is kept

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
    // The classic pass of PCLAB over the block, in the textbook order when the banks were found
    int textbook;
    u32 kernel_banks;  // banks of the kernel of the bank function, 0 when it was not found
    int prefetch_off;  // the hardware prefetchers were off during the classic pass
    u64 max_dist, dist_step;
    usize classic_n;
    textbook_peaks peaks;
    u32 t1_banks;   // T1 / line
    u64 t2_page;    // T2 / banks
    u64 t2_pages;   // memory / page
    int valid;
} R;

static u64 classic_dist[CLASSIC_POINTS];
static double classic_us[CLASSIC_POINTS];

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

#ifdef ROWCONF_SYNTHETIC
// The classic pass on the model: reads in one bank wait for each other, a row change costs more
static u64 classic_ticks(const u8 *block, const u32 *order, u32 lines, u64 dist) {
    static u32 open_row[16];
    for (int k = 0; k < 16; k++) open_row[k] = ~0u;
    u32 last = ~0u;
    u64 t = 0, total = (u64)lines * ROWCONF_LINE;
    for (u64 b = 0; b < dist; b += ROWCONF_LINE) {
        for (u64 a = b; a < total; a += dist) {
            u64 x = (u64)block + (u64)order[a / ROWCONF_LINE] * ROWCONF_LINE;
            u32 k = model_bank(x), row = (u32)(x >> 17);
            int hit = open_row[k] == row;
            t += k == last ? (hit ? 30 : 140) : (hit ? 10 : 50);
            open_row[k] = row, last = k;
        }
    }
    return t + (rdtsc() * 2654435761u >> 40) % (t / 40 + 1);
}

#else
// Every line of the block once, in the order of the PCLAB loop: the ones `dist` apart one after
// another. The caches are empty, so every read goes to DRAM
static u64 classic_ticks(const u8 *block, const u32 *order, u32 lines, u64 dist) {
    u64 sum = 0;
    for (u32 i = 0; i < lines; i += 16) sum += order[i];  // the order comes into the cache, the block does not
    u64 total = (u64)lines * ROWCONF_LINE;
    usize q = quiet_begin();
    u64 t0 = tsc_begin();
    for (u64 b = 0; b < dist; b += ROWCONF_LINE) {
        for (u64 a = b; a < total; a += dist) {
            sum += *(const volatile u64 *)(block + (u64)order[a / ROWCONF_LINE] * ROWCONF_LINE);
        }
    }
    u64 t = tsc_end() - t0;
    quiet_end(q);
    keep(sum);
    return t;
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

typedef enum { OK, NO_TIMES, NO_PEAK, NO_SECOND_PEAK, ODD_NUMBERS } verdict;

static verdict judge(void) {
    if (R.a.modes.low < 1) return NO_TIMES;
    if (R.a.modes.high <= 0 || !R.ref2) return NO_PEAK;
    if (R.b.modes.high <= 0) return NO_SECOND_PEAK;
    if (R.estimate.banks < 2 || R.estimate.banks > 512 || R.estimate.row_bytes < 1024) return ODD_NUMBERS;
    return OK;
}

// The textbook order from the bank of line 0: the kernel of the bank function, the bank of every line,
// then the banks taking turns line by line. Returns 0 when the kernel does not give a sane bank count
static int build_order(u32 *order, u32 *bank_of, u32 *scratch) {
    u8 *in_bank = (u8 *)scratch;  // free until bank_numbers fills scratch
    const u8 *fa = R.flags, *fb = R.flags + R.lines;
    for (u32 i = 0; i < R.lines; i++) in_bank[i] = fa[i] || fb[i];
    in_bank[0] = 1;
    static u32 basis[TEXTBOOK_MAX_RANK];
    usize rank = bank_kernel(in_bank, R.lines, 0, basis);
    u32 banks = bank_numbers(basis, rank, R.lines, bank_of, scratch);
    if (banks < 2 || banks > 512) return 0;
    R.kernel_banks = banks;
    textbook_order(bank_of, R.lines, banks, order, scratch);
    return 1;
}

static void classic_graph(const u8 *block, const u32 *order) {
    R.classic_n = 0;
    for (u64 d = R.dist_step; d <= R.max_dist && R.classic_n < CLASSIC_POINTS; d += R.dist_step) {
        classic_dist[R.classic_n++] = d;
    }
    R.prefetch_off = hw_prefetch_off();
    for (usize i = 0; i < R.classic_n; i++) {
        if ((i & 63) == 0) progress(i, R.classic_n);
        u64 best = ~0ull;
        for (int r = 0; r < CLASSIC_REPEATS; r++) {
            flush_caches();
            u64 t = classic_ticks(block, order, R.lines, classic_dist[i]);
            if (t < best) best = t;
        }
        classic_us[i] = ticks_ns(best) / 1000;
    }
    hw_prefetch_restore();
    R.peaks = find_textbook_peaks(classic_dist, classic_us, R.classic_n, ROWCONF_LINE);
    R.t1_banks = R.peaks.t1 < R.classic_n ? (u32)(classic_dist[R.peaks.t1] / ROWCONF_LINE) : 0;
    R.t2_page = R.peaks.t2 < R.classic_n && R.t1_banks ? classic_dist[R.peaks.t2] / R.t1_banks : 0;
    R.t2_pages = R.t2_page ? R.memory_bytes / R.t2_page : 0;
}

int rowconf_run(const exp_param *params) {
    // A power of two of megabytes on a boundary of its size: the line number is then the address bits
    u64 mb = (u64)params[0].value;
    while (mb & (mb - 1)) mb &= mb - 1;
    u64 block_bytes = mb * MB;
    int rounds = (int)params[1].value;
    if (rounds < 1) rounds = 1;
    if (rounds > MAX_ROUNDS) rounds = MAX_ROUNDS;
    R.dist_step = ((u64)params[3].value + ROWCONF_LINE - 1) / ROWCONF_LINE * ROWCONF_LINE;
    R.max_dist = (u64)params[2].value * 1024;
    if (R.max_dist > block_bytes / 2) R.max_dist = block_bytes / 2;
    if (R.max_dist < 4 * R.dist_step) R.max_dist = 4 * R.dist_step;
    u32 lines = (u32)(block_bytes / ROWCONF_LINE);
    if (!prepare_result(lines)) return 0;
    usize block_pages = (usize)(2 * block_bytes / 4096), work_pages = ((usize)lines * 12 + 4095) / 4096;
    u8 *raw = alloc_pages(block_pages);
    u32 *work = alloc_pages(work_pages);
    if (!raw || !work) {
        if (raw) BS->free_pages((efi_phys_addr)raw, block_pages);
        if (work) BS->free_pages((efi_phys_addr)work, work_pages);
        return 0;
    }
    u8 *block = (u8 *)(((u64)raw + block_bytes - 1) & ~(block_bytes - 1));
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
    memset(R.flags, 0, 2 * (usize)R.lines);
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

    u32 *order = work, *bank_of = work + lines, *scratch = work + 2 * (usize)lines;
    R.kernel_banks = 0;
    R.textbook = judge() == OK && build_order(order, bank_of, scratch);
    if (!R.textbook) {
        for (u32 i = 0; i < lines; i++) order[i] = i;
    }
    classic_graph(block, order);
    R.duration_us = now_us() - start;
    R.valid = 1;
    BS->free_pages((efi_phys_addr)raw, block_pages);
    BS->free_pages((efi_phys_addr)work, work_pages);
    log_line("rowconf: low %.1f high %.1f threshold %.1f contrast %.1f peak %lu retried %lu; ref2 %u; "
             "banks %u row %u B; kernel banks %u, textbook %d, prefetch off %d; T1 %lu T2 %lu; %lu us",
             R.a.modes.low, R.a.modes.high, R.a.modes.threshold, R.a.modes.contrast, R.a.modes.high_count,
             R.a.retried, R.ref2 * ROWCONF_LINE, R.estimate.banks, R.estimate.row_bytes, R.kernel_banks, R.textbook,
             R.prefetch_off, R.peaks.t1 < R.classic_n ? classic_dist[R.peaks.t1] : 0,
             R.peaks.t2 < R.classic_n ? classic_dist[R.peaks.t2] : 0, R.duration_us);
    return 1;
}

// ==========================
// ===  Verdict           ===
// ==========================

static const char *const VERDICT_TEXT[] = {
    [OK] = "ok",
    [NO_TIMES] = "no_times",
    [NO_PEAK] = "no_peak",
    [NO_SECOND_PEAK] = "no_second_peak",
    [ODD_NUMBERS] = "odd_numbers",
};

// T1 and T2 by the formulas of the lab manual, then how the order was made
static void draw_classic_verdict(i32 x, i32 y, i32 width) {
    char line[400], t1[32], t2[32], page[32], pages[32], mem[32];
    format_bytes(mem, sizeof mem, (double)R.memory_bytes);
    i32 h;
    if (!R.textbook) {
        h = wrap_text(x, y, width, "Банки не разложены: классический график построен в физическом порядке линеек.",
                      COL_PEACH);
    } else if (R.peaks.t1 >= R.classic_n || !R.t2_page) {
        fmt(line, sizeof line, "Пик T1 не найден: на кратных расстояниях время выше обычного меньше чем в %.1f раза.",
            TEXTBOOK_T1_LEVEL);
        h = wrap_text(x, y, width, line, COL_PEACH);
    } else {
        format_bytes(t1, sizeof t1, (double)classic_dist[R.peaks.t1]);
        format_bytes(t2, sizeof t2, (double)classic_dist[R.peaks.t2]);
        format_bytes(page, sizeof page, (double)R.t2_page);
        format_count(pages, sizeof pages, R.t2_pages);
        fmt(line, sizeof line, "T1 = %s, Б = T1 / линейка = %u.   T2 = %s, РС = T2 / Б = %s.   С = О / РС = %s при О = %s.",
            t1, R.t1_banks, t2, page, pages, mem);
        h = wrap_text(x, y, width, line, COL_TEXT);
    }
    if (R.textbook) {
        fmt(line, sizeof line,
            "Линейки в учебном порядке: labos нашёл %u банков по конфликтам строк и разложил линейки по кругу банков. "
            "Без конфликта пара читается за %.0f нс, с конфликтом строк за %.0f нс.",
            R.kernel_banks, R.a.modes.low, R.a.modes.high);
        h += wrap_text(x, y + h + 2, width, line, COL_SUBTEXT) + 2;
    }
    wrap_text(x, y + h + 2, width,
              R.prefetch_off ? "Аппаратная предвыборка выключена на время классического опыта."
                             : "Аппаратная предвыборка осталась включённой: этот процессор не из известного списка.",
              COL_SUBTEXT);
}

static void draw_verdict(i32 x, i32 y, i32 width) {
    char line[320], row[32];
    const time_modes *m = &R.a.modes;
    switch (judge()) {
        case OK:
            draw_classic_verdict(x, y, width);
            return;
        case NO_TIMES:
            fmt(line, sizeof line, "Времена почти нулевые: у этой машины нет настоящей DRAM (виртуальная машина).");
            break;
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
    i32 used = wrap_text(x, y, width, line, COL_PEACH);
    wrap_text(x, y + used + 2, width, "Поэтому классический график построен в физическом порядке линеек, как в PCLAB.",
              COL_SUBTEXT);
}

// ==========================
// ===  Screen            ===
// ==========================

// The classic graph up to n points: the whole range as dots, where T2 shows, or its start as a line,
// where the peaks of T1 stand apart
static void draw_classic(i32 x, i32 y, i32 w, i32 h, usize n, int whole) {
    if (n < 2) return;
    static double xs[CLASSIC_POINTS];
    for (usize i = 0; i < n; i++) xs[i] = (double)classic_dist[i];
    const char *title = whole ? (R.textbook ? "Время чтения блока: линейки в учебном порядке"
                                            : "Время чтения блока: линейки в физическом порядке")
                              : "Начало графика: пики T1";
    plot p = {.x = x, .y = y, .w = w, .h = h, .x0 = 0, .x1 = xs[n - 1], .y0 = 0, .y1 = axis_top(classic_us, n),
              .x_kind = AXIS_BYTES, .title = title, .x_label = whole ? 0 : "Расстояние между читаемыми ячейками",
              .y_label = "мкс", .legend_row = whole};
    plot_frame(&p);
    int t1 = R.peaks.t1 < n, t2 = R.peaks.t2 < n;
    if (t1) plot_vline(&p, xs[R.peaks.t1], COL_PEACH);
    if (t2) plot_vline(&p, xs[R.peaks.t2], COL_RED);
    if (whole) {
        plot_dots(&p, xs, classic_us, n, COL_BLUE, 1.0);
    } else {
        plot_line(&p, xs, classic_us, n, COL_BLUE);
    }
    if (!whole) return;
    legend_item items[3] = {{"время чтения", COL_BLUE}};
    usize k = 1;
    if (R.peaks.t1 < R.classic_n) items[k++] = (legend_item){"T1", COL_PEACH};
    if (t2) items[k++] = (legend_item){"T2", COL_RED};
    plot_legend(&p, items, k);
}

// Points of the start of the graph: eight times T1, or 16 KB when T1 is not found
static usize zoom_points(void) {
    u64 upto = R.peaks.t1 < R.classic_n ? 8 * classic_dist[R.peaks.t1] : 16384;
    usize n = 0;
    while (n < R.classic_n && classic_dist[n] <= upto) n++;
    return n;
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
              .title = "Поиск банков: время пар", .x_label = "Время пары, нс",
              .y_label = "линеек", .legend_row = 1};
    plot_frame(&p);
    plot_bars(&p, left, width, low_counts, HIST_BINS, COL_BLUE);
    plot_bars(&p, left, width, high_counts, HIST_BINS, COL_RED);
    if (m->high > 0) plot_vline(&p, m->threshold, COL_PEACH);
    legend_item items[] = {{"обычные", COL_BLUE}, {"конфликт", COL_RED}};
    plot_legend(&p, items, m->high > 0 ? 2 : 1);
}

void rowconf_draw(i32 height) {
    gfx_fill(0, 0, screen.width, height, COL_OUTER);
    if (!R.valid) return;
    i32 lh = gfx_line_height(), text_h = 4 * (lh + 6) + 16;
    i32 graph_h = height - text_h, split = (i32)screen.width * 66 / 100;
    i32 top_h = graph_h * 58 / 100;
    draw_classic(0, 0, split, top_h, R.classic_n, 1);
    draw_classic(0, top_h, split, graph_h - top_h, zoom_points(), 0);
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
                  "memory_bytes %lu\npages %lu\nduration_us %lu\ntsc_ghz %.4f\n",
                  VERDICT_TEXT[judge()], R.base, (u64)R.lines * ROWCONF_LINE, R.rounds, a->low, a->high, a->threshold,
                  a->contrast, a->high_count, R.a.retried, R.ref2 * ROWCONF_LINE, b->low, b->high, b->contrast,
                  R.estimate.banks, R.estimate.row_bytes, R.estimate.same_bank, R.estimate.row_lines,
                  R.estimate.lines, R.memory_bytes, R.pages, R.duration_us, tsc_per_ns);
    int t1 = R.peaks.t1 < R.classic_n, t2 = R.peaks.t2 < R.classic_n;
    n += fmt(s + n, cap - n,
             "order %s\nkernel_banks %u\nprefetch_off %d\nmax_distance_bytes %lu\ndistance_step_bytes %lu\n"
             "t1_bytes %lu\nt1_level %.3f\nt2_bytes %lu\nfast_us %.3f\nbanks_t1 %u\npage_bytes_t2 %lu\npages_t2 %lu\n"
             "classic distance_bytes time_us\n",
             R.textbook ? "textbook" : "physical", R.kernel_banks, R.prefetch_off, R.max_dist, R.dist_step,
             t1 ? classic_dist[R.peaks.t1] : 0, R.peaks.t1_level, t2 ? classic_dist[R.peaks.t2] : 0, R.peaks.fast,
             R.t1_banks, R.t2_page, R.t2_pages);
    for (usize i = 0; i < R.classic_n && n + 48 < cap; i++) {
        n += fmt(s + n, cap - n, "%lu %.3f\n", classic_dist[i], classic_us[i]);
    }
    n += fmt(s + n, cap - n, "data offset ns ns2\n");
    for (u32 i = 0; i < R.lines && n + 48 < cap; i++) {
        n += fmt(s + n, cap - n, "%u %.1f %.1f\n", i * ROWCONF_LINE, R.a.ns[i], R.b.ns[i]);
    }
    return n;
}
