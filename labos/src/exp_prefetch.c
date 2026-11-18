// Experiment 3: the first access to a page is slow: the processor walks the page tables and opens a DRAM
// row. A prefetch pass touches every page first; then the reads find the page in the TLB and its first
// line in the cache. labos maps the block with 4 KB pages (pages4k.c), empties the caches with wbinvd and
// the TLB with a CR3 reload before every pass, and keeps interrupts off during the passes.
#include "exp_prefetch.h"

#include "expscreen.h"
#include "gfx.h"
#include "measure.h"
#include "pages4k.h"
#include "plotmath.h"

#define PAGE 4096u
#define MB2 (2ull << 20)
#define PLOT_MAX 4096  // accesses on the graph; the statistics take them all
#define REPEATS 3

static struct {
    u32 *offsets;  // the walk: pages shuffled or in a row, sequential inside a page
    u32 *t_no, *t_pf;  // ticks of every access without and with the prefetch pass
    usize accesses, pages;
    usize mem_pages;
    double plot_x[PLOT_MAX], plot_no[PLOT_MAX], plot_pf[PLOT_MAX];
    usize plotted;
    double first_no_ns, first_pf_ns, other_ns, ratio, ratio_with_loop, overhead_ns;
    int shuffled, small_pages, valid;
    u64 step, block;
} P;

static u64 timer_overhead(void) {
    u64 best = ~0ull;
    for (int i = 0; i < 1000; i++) {
        u64 t0 = tsc_begin(), t = tsc_end() - t0;
        if (t < best) best = t;
    }
    return best;
}

static void build_walk(u64 shuffled) {
    u32 per_page = (u32)((PAGE + P.step - 1) / P.step);
    u32 *order = (u32 *)P.t_pf;  // free until the timed passes
    for (u32 i = 0; i < P.pages; i++) order[i] = i;
    u64 state = 12345;
    for (u32 i = (u32)P.pages; shuffled && i > 1; i--) {
        u32 j = lcg_next(&state) % i, t = order[i - 1];
        order[i - 1] = order[j], order[j] = t;
    }
    usize n = 0;
    for (usize k = 0; k < P.pages; k++) {
        for (u32 j = 0; j < per_page && (u64)j * P.step < PAGE && (u64)order[k] * PAGE + (u64)j * P.step < P.block; j++) {
            P.offsets[n++] = order[k] * PAGE + j * (u32)P.step;
        }
    }
    P.accesses = n;
}

static void timed_pass(const volatile u8 *base, u32 *ticks) {
    for (usize i = 0; i < P.accesses; i++) {
        u64 t0 = tsc_begin();
        (void)*(const volatile i32 *)(base + P.offsets[i]);
        ticks[i] = (u32)(tsc_end() - t0);
    }
}

static u64 prefetch_loop(const volatile u8 *base) {
    u64 t0 = tsc_begin();
    for (usize k = 0; k < P.accesses; k++) {
        if (!k || P.offsets[k] / PAGE != P.offsets[k - 1] / PAGE) (void)*(const volatile i32 *)(base + P.offsets[k] / PAGE * PAGE);
    }
    return tsc_end() - t0;
}

static u64 block_pass(const volatile u8 *base) {
    u64 t0 = tsc_begin(), sum = 0;
    for (usize i = 0; i < P.accesses; i++) sum += (u64)*(const volatile i32 *)(base + P.offsets[i]);
    u64 t = tsc_end() - t0;
    keep(sum);
    return t;
}

static void summarize(u64 overhead) {
    double first_no = 0, first_pf = 0, other = 0;
    usize firsts = 0, others = 0;
    for (usize i = 0; i < P.accesses; i++) {
        double no = ticks_ns(P.t_no[i] > overhead ? P.t_no[i] - overhead : 0);
        double pf = ticks_ns(P.t_pf[i] > overhead ? P.t_pf[i] - overhead : 0);
        if (!i || P.offsets[i] / PAGE != P.offsets[i - 1] / PAGE) {
            first_no += no, first_pf += pf, firsts++;
        } else {
            other += no, others++;
        }
        if (i < PLOT_MAX) P.plot_x[i] = (double)i * (double)P.step, P.plot_no[i] = no, P.plot_pf[i] = pf;
    }
    P.plotted = P.accesses < PLOT_MAX ? P.accesses : PLOT_MAX;
    P.first_no_ns = firsts ? first_no / firsts : 0;
    P.first_pf_ns = firsts ? first_pf / firsts : 0;
    P.other_ns = others ? other / others : 0;
}

int prefetch_run(const exp_param *params) {
    P.step = (u64)params[0].value;
    P.block = (u64)params[1].value * 1024;
    P.shuffled = params[2].value != 0;
    P.pages = (P.block + PAGE - 1) / PAGE;
    usize max_accesses = P.pages * ((PAGE + P.step - 1) / P.step);
    // The block on a 2 MB boundary, whole 2 MB pieces: the piece the 4 KB pages replace
    u64 span = (P.block + MB2 - 1) / MB2 * MB2;
    usize block_pages = (span + MB2) / 4096;
    u8 *raw = alloc_pages(block_pages);
    if (P.offsets) BS->free_pages((efi_phys_addr)P.offsets, P.mem_pages);
    P.mem_pages = (max_accesses * 12 + 4095) / 4096;
    P.offsets = alloc_pages(P.mem_pages);
    if (!raw || !P.offsets) {
        if (raw) BS->free_pages((efi_phys_addr)raw, block_pages);
        P.offsets = 0;
        return 0;
    }
    P.t_no = P.offsets + max_accesses;
    P.t_pf = P.t_no + max_accesses;
    u8 *base = (u8 *)(((u64)raw + MB2 - 1) & ~(MB2 - 1));
    memset(base, 1, span);
    build_walk(P.shuffled);
    P.small_pages = pages4k_begin((u64)base, span);
    u64 overhead = timer_overhead();

    usize q = quiet_begin();
    flush_caches(), flush_tlb();
    timed_pass(base, P.t_no);
    flush_caches(), flush_tlb();
    prefetch_loop(base);
    timed_pass(base, P.t_pf);
    quiet_end(q);

    // The ratio is taken over whole passes without per access timers: they would dominate the sum
    u64 best_no = ~0ull, best_pf = ~0ull, best_loop = 0;
    for (int r = 0; r < REPEATS; r++) {
        progress((u64)r, REPEATS);
        q = quiet_begin();
        flush_caches(), flush_tlb();
        u64 no = block_pass(base);
        flush_caches(), flush_tlb();
        u64 loop = prefetch_loop(base);
        u64 pf = block_pass(base);
        quiet_end(q);
        if (no < best_no) best_no = no;
        if (pf < best_pf) best_pf = pf, best_loop = loop;
    }
    pages4k_end();
    BS->free_pages((efi_phys_addr)raw, block_pages);
    summarize(overhead);
    P.overhead_ns = ticks_ns(overhead);
    P.ratio = best_pf ? (double)best_no / (double)best_pf : 0;
    P.ratio_with_loop = best_pf ? (double)best_no / (double)(best_pf + best_loop) : 0;
    P.valid = 1;
    log_line("prefetch: %lu accesses, %lu pages, 4 KB pages %s, first %.1f / %.1f ns, ratio %.2f", P.accesses,
             P.pages, P.small_pages ? "yes" : "no", P.first_no_ns, P.first_pf_ns, P.ratio);
    return 1;
}

void prefetch_draw(i32 height) {
    gfx_fill(0, 0, screen.width, height, COL_OUTER);
    if (!P.valid) return;
    static char c0[200], c1[220], c2[220];
    fmt(c0, sizeof c0, "Обработка без предвыборки шла в %.2f раза дольше (с учётом цикла предвыборки в %.2f раза).",
        P.ratio, P.ratio_with_loop);
    fmt(c1, sizeof c1, "Первое обращение к странице: %.0f нс без предвыборки, %.0f нс с ней. Остальные обращения: %.1f нс.",
        P.first_no_ns, P.first_pf_ns, P.other_ns);
    fmt(c2, sizeof c2, "Страниц %lu по 4 КБ%s, обход страниц %s, обращений %lu.%s", P.pages,
        P.small_pages ? "" : " (не удалось: блок в больших страницах UEFI)", P.shuffled ? "вразброс" : "подряд",
        P.accesses, P.accesses > PLOT_MAX ? " На графике первые 4096 обращений." : "");
    series lines[] = {{"Без предвыборки", COL_RED, P.plot_x, P.plot_no, P.plotted},
                      {"С предвыборкой", COL_GREEN, P.plot_x, P.plot_pf, P.plotted}};
    double lo, hi;
    min_max(P.plot_no, P.plotted, &lo, &hi);
    double top = P.first_no_ns * 1.6 > hi ? hi * 1.08 : P.first_no_ns * 1.6;  // rare interrupts stay off the scale
    expscreen s = {.graph = {.x0 = 0, .x1 = P.plot_x[P.plotted - 1] + 1, .y0 = 0, .y1 = top > 1 ? top : 1,
                             .x_kind = AXIS_BYTES, .title = "Исследование эффективности программной предвыборки",
                             .x_label = P.shuffled ? "Смещение в порядке обхода (страницы вразброс)" : "Смещение",
                             .y_label = "нс"},
                   .lines = lines, .nlines = 2, .conclusions = {c0, c1, c2}};
    expscreen_draw(&s, height);
}

usize prefetch_report(u8 *out, usize cap) {
    char *s = (char *)out;
    if (!P.valid) return fmt(s, cap, "error: experiment 3 has not run yet");
    usize n = fmt(s, cap,
                  "experiment prefetch\nstep_bytes %lu\nblock_bytes %lu\npages %lu\nshuffled %d\nsmall_pages %d\n"
                  "accesses %lu\nratio %.3f\nratio_with_loop %.3f\nfirst_no_prefetch_ns %.2f\nfirst_prefetch_ns %.2f\n"
                  "other_ns %.2f\ntimer_overhead_ns %.2f\ntsc_ghz %.4f\ndata offset no_prefetch_ns prefetch_ns\n",
                  P.step, P.block, P.pages, P.shuffled, P.small_pages, P.accesses, P.ratio, P.ratio_with_loop,
                  P.first_no_ns, P.first_pf_ns, P.other_ns, P.overhead_ns, tsc_per_ns);
    for (usize i = 0; i < P.plotted && n + 64 < cap; i++) {
        n += fmt(s + n, cap - n, "%u %.1f %.1f\n", P.offsets[i], P.plot_no[i], P.plot_pf[i]);
    }
    return n;
}
