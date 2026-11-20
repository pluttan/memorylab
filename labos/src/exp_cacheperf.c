// Experiment 6 (PCLAB «CashReadWrite»): the block grows, and for every size labos times one kilobyte of
// it in four modes: read, write, read and write, write and then read. While the block fits into a
// cache level, the time per kilobyte stays flat; past the level it rises in a step. One access per cache
// line: a word by word pass would be limited by the instructions, and the L1 and L2 steps would vanish.
#include "exp_cacheperf.h"

#include "cachesteps.h"
#include "exp_cpuinfo.h"
#include "expscreen.h"
#include "gfx.h"
#include "measure.h"
#include "plotmath.h"

#define MAX_POINTS 512
#define MODES 4
#define REPEATS 3
#define MIN_BYTES (32ull << 20)  // bytes processed in one measurement

static const char *const MODE_NAME[MODES] = {"Чтение", "Запись", "Чтение и запись", "Запись и чтение"};
static const u32 MODE_COLOR[MODES] = {COL_RED, COL_GREEN, COL_MAUVE, COL_BLUE};  // as in the manual

static struct {
    u64 sizes[MAX_POINTS];
    double xs[MAX_POINTS], ns_per_kb[MODES][MAX_POINTS];
    usize points;
    u64 line, bounds[3], cpuid[3];
    usize nbounds;
    double marks[3];
    usize nmarks;
    int valid;
} C6;

#define BARRIER() __asm__ volatile("" ::: "memory")

// Four independent sums: the additions do not wait for each other, and the cache sets the speed
static u64 read_pass(const u64 *words, usize count, usize stride) {
    u64 s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    usize i = 0;
    for (; i + 3 * stride < count; i += 4 * stride) {
        s0 += words[i], s1 += words[i + stride], s2 += words[i + 2 * stride], s3 += words[i + 3 * stride];
    }
    for (; i < count; i += stride) s0 += words[i];
    return s0 + s1 + s2 + s3;
}

static void write_pass(u64 *words, usize count, usize stride, u64 value) {
    for (usize i = 0; i < count; i += stride) words[i] = value;
}

static void read_write_pass(u64 *words, usize count, usize stride) {
    for (usize i = 0; i < count; i += stride) words[i] += 1;
}

static u64 run_passes(int mode, u64 *words, usize count, usize stride, usize passes) {
    u64 sink = 0;
    for (usize pass = 0; pass < passes; pass++) {
        if (mode == 0) sink += read_pass(words, count, stride);
        if (mode == 1) write_pass(words, count, stride, pass);
        if (mode == 2) read_write_pass(words, count, stride);
        if (mode == 3) {
            write_pass(words, count, stride, pass);
            BARRIER();
            sink += read_pass(words, count, stride);
        }
        BARRIER();
    }
    return sink;
}

static double ns_per_kb(int mode, u64 *words, u64 block, usize stride) {
    usize count = (usize)(block / 8);
    usize passes = block < MIN_BYTES ? (usize)(MIN_BYTES / block) : 1;
    u64 sink = run_passes(mode, words, count, stride, 1);  // warm up: the block comes into the cache
    u64 best = ~0ull;
    for (int r = 0; r < REPEATS; r++) {
        usize q = quiet_begin();
        u64 t0 = tsc_begin();
        sink += run_passes(mode, words, count, stride, passes);
        u64 t = tsc_end() - t0;
        quiet_end(q);
        if (t < best) best = t;
    }
    keep(sink);
    return ticks_ns(best) / (double)passes / ((double)block / 1024);
}

int cacheperf_run(const exp_param *params) {
    u64 max_bytes = (u64)params[0].value * 1024, step = (u64)params[1].value;
    C6.line = cpu_line_bytes();
    C6.points = cache_block_sizes(step, max_bytes, C6.line, C6.sizes, MAX_POINTS);
    if (!C6.points) return 0;
    u64 top = C6.sizes[C6.points - 1];
    usize pages = (usize)((top + 4095) / 4096);
    u64 *words = alloc_pages(pages);
    if (!words) return 0;
    memset(words, 0, pages * 4096);
    usize stride = C6.line / 8 ? (usize)(C6.line / 8) : 1;
    for (usize i = 0; i < C6.points; i++) {
        progress(i, C6.points);
        C6.xs[i] = (double)C6.sizes[i];
        for (int m = 0; m < MODES; m++) C6.ns_per_kb[m][i] = ns_per_kb(m, words, C6.sizes[i], stride);
    }
    BS->free_pages((efi_phys_addr)words, pages);
    C6.nbounds = cache_steps(C6.sizes, C6.ns_per_kb[0], C6.points, C6.bounds);
    C6.nmarks = 0;
    for (int l = 0; l < 3; l++) {
        C6.cpuid[l] = cpu_cache_bytes(l + 1);
        if (C6.cpuid[l] && C6.cpuid[l] >= C6.sizes[0] && C6.cpuid[l] <= top) C6.marks[C6.nmarks++] = (double)C6.cpuid[l];
    }
    C6.valid = 1;
    log_line("cacheperf: %lu points up to %lu KB, %lu steps", C6.points, top >> 10, C6.nbounds);
    return 1;
}

static void list_sizes(char *out, usize cap, const u64 *v, usize n) {
    usize len = 0;
    out[0] = 0;
    for (usize i = 0; i < n; i++) {
        char one[32];
        format_bytes(one, sizeof one, (double)v[i]);
        len += fmt(out + len, cap - len, "%s%s", i ? ", " : "", one);
    }
}

void cacheperf_draw(i32 height) {
    gfx_fill(0, 0, screen.width, height, COL_OUTER);
    if (!C6.valid) return;
    static char c0[200], c1[200], c2[200];
    char steps[96], cpuid[128];
    list_sizes(steps, sizeof steps, C6.bounds, C6.nbounds);
    if (C6.nbounds) {
        fmt(c0, sizeof c0, "Ступени графика чтения: %s.", steps);
    } else {
        fmt(c0, sizeof c0, "На графике чтения нет ступеней выше %.1f раза. Увеличьте наибольший блок.", CACHE_JUMP);
    }
    usize len = 0;
    const char *names[3] = {"L1", "L2", "L3"};
    cpuid[0] = 0;
    for (int l = 0; l < 3; l++) {
        if (!C6.cpuid[l]) continue;
        char one[32];
        format_bytes(one, sizeof one, (double)C6.cpuid[l]);
        len += fmt(cpuid + len, sizeof cpuid - len, "%s%s %s", len ? ", " : "", names[l], one);
    }
    fmt(c1, sizeof c1, "Объём кэша по команде CPUID: %s. Пунктир на графике отмечает эти объёмы.", cpuid);
    char top[32];
    format_bytes(top, sizeof top, C6.xs[C6.points - 1]);
    fmt(c2, sizeof c2, "Блоков %lu, до %s. Одно обращение на линейку %lu Б. Время: наименьшее из %d.", C6.points,
        top, C6.line, REPEATS);
    series lines[MODES];
    double hi = 0;
    for (int m = 0; m < MODES; m++) {
        lines[m] = (series){MODE_NAME[m], MODE_COLOR[m], C6.xs, C6.ns_per_kb[m], C6.points};
        double t = axis_top(C6.ns_per_kb[m], C6.points);
        if (t > hi) hi = t;
    }
    expscreen s = {.graph = {.x0 = C6.xs[0], .x1 = C6.xs[C6.points - 1] > C6.xs[0] ? C6.xs[C6.points - 1] : C6.xs[0] * 2,
                             .y0 = 0, .y1 = hi, .log_x = 1, .x_kind = AXIS_BYTES,
                             .title = "Исследование производительности кэш-памяти",
                             .x_label = "Размер блока", .y_label = "нс/КБ"},
                   .lines = lines, .nlines = MODES, .marks = C6.marks, .nmarks = C6.nmarks,
                   .mark_name = "Объёмы по CPUID", .conclusions = {c0, c1, c2}};
    expscreen_draw(&s, height);
}

usize cacheperf_report(u8 *out, usize cap) {
    char *s = (char *)out;
    if (!C6.valid) return fmt(s, cap, "error: experiment 6 has not run yet");
    usize n = fmt(s, cap, "experiment cache_performance\nline_bytes %lu\npoints %lu\nstep1_bytes %lu\nstep2_bytes %lu\n"
                          "step3_bytes %lu\ncpuid_l1_bytes %lu\ncpuid_l2_bytes %lu\ncpuid_l3_bytes %lu\ntsc_ghz %.4f\n"
                          "data block_bytes read_ns_per_kb write_ns_per_kb read_write_ns_per_kb write_read_ns_per_kb\n",
                  C6.line, C6.points, C6.nbounds > 0 ? C6.bounds[0] : 0, C6.nbounds > 1 ? C6.bounds[1] : 0,
                  C6.nbounds > 2 ? C6.bounds[2] : 0, C6.cpuid[0], C6.cpuid[1], C6.cpuid[2], tsc_per_ns);
    for (usize i = 0; i < C6.points && n + 96 < cap; i++) {
        n += fmt(s + n, cap - n, "%lu %.2f %.2f %.2f %.2f\n", C6.sizes[i], C6.ns_per_kb[0][i], C6.ns_per_kb[1][i],
                 C6.ns_per_kb[2][i], C6.ns_per_kb[3][i]);
    }
    return n;
}
