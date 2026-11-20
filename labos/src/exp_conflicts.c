// Experiment 5: lines a bank size apart fall into one set of the cache. When there are more of them
// than ways in the set, they push each other out, and every read goes to the next level. The lines are
// linked into a ring in a shuffled order, each keeps the address of the next one: the loads wait for
// each other, so the time is the latency of the cache, and the hardware prefetch cannot guess the next
// address. UEFI maps the memory one to one, so the set of L2 and L3 is chosen by this very address.
#include "exp_conflicts.h"

#include "expscreen.h"
#include "gfx.h"
#include "measure.h"
#include "plotmath.h"

#define MAX_LINES 512
#define REPEATS 3
#define MIN_HOPS 32768  // reads in one measurement: far longer than the timer step
#define JUMP 1.5  // conflicts are counted where the time is this many times longer

static struct {
    double xs[MAX_LINES], conflict_ns[MAX_LINES], free_ns[MAX_LINES];
    // As in the manual: the offset of the last line read, a * bank and a * (bank + line)
    double conflict_off[MAX_LINES], free_off[MAX_LINES];
    double threshold_off;
    usize points;
    u64 bank, line;
    double threshold;  // the smallest number of lines with conflicts, or 0
    double ratio;
    int valid;
} K;

// Mean time of one read while n lines with the given stride are read round and round
static double chase_ns(u8 *base, u64 stride, u32 n) {
    static u32 order[MAX_LINES];
    for (u32 i = 0; i < n; i++) order[i] = i;
    u64 state = 12345 + n;
    for (u32 i = n; i > 1; i--) {
        u32 j = lcg_next(&state) % i, t = order[i - 1];
        order[i - 1] = order[j], order[j] = t;
    }
    for (u32 k = 0; k < n; k++) *(u8 **)(base + order[k] * stride) = base + order[(k + 1) % n] * stride;
    u64 hops = (u64)n * 64 > MIN_HOPS ? (u64)n * 64 : MIN_HOPS;
    u64 best = ~0ull;
    for (int r = 0; r < REPEATS; r++) {
        u8 *p = base + order[0] * stride;
        usize q = quiet_begin();
        for (u32 k = 0; k < n; k++) p = *(u8 **)p;  // one round to warm the cache up
        u64 t0 = tsc_begin();
        for (u64 h = 0; h < hops; h++) p = *(u8 **)p;
        u64 t = tsc_end() - t0;
        quiet_end(q);
        keep((u64)p);
        if (t < best) best = t;
    }
    return ticks_ns(best) / (double)hops;
}

int conflicts_run(const exp_param *params) {
    K.bank = (u64)params[0].value * 1024;
    K.line = (u64)params[1].value;
    u32 max_lines = (u32)params[2].value;
    if (K.line < sizeof(void *)) K.line = sizeof(void *);
    usize pages = (usize)(((K.bank + K.line) * max_lines + K.bank + 4095) / 4096);
    u8 *base = alloc_pages(pages);
    if (!base) return 0;
    memset(base, 0, pages * 4096);
    K.points = 0;
    for (u32 n = 1; n <= max_lines; n++) {
        progress(n - 1, max_lines);
        K.xs[K.points] = n;
        K.conflict_off[K.points] = (double)(n - 1) * (double)K.bank;
        K.free_off[K.points] = (double)(n - 1) * (double)(K.bank + K.line);
        K.conflict_ns[K.points] = chase_ns(base, K.bank, n);
        K.free_ns[K.points] = chase_ns(base, K.bank + K.line, n);
        K.points++;
    }
    BS->free_pages((efi_phys_addr)base, pages);
    K.threshold = 0, K.threshold_off = 0;
    for (usize i = 0; i < K.points && !K.threshold; i++) {
        if (K.conflict_ns[i] > JUMP * K.free_ns[i]) K.threshold = K.xs[i], K.threshold_off = K.conflict_off[i];
    }
    double last_free = K.free_ns[K.points - 1];
    K.ratio = last_free > 0 ? K.conflict_ns[K.points - 1] / last_free : 0;
    K.valid = 1;
    log_line("conflicts: bank %lu, line %lu, %u lines, from %d lines, ratio %.2f", K.bank, K.line, max_lines,
             (int)K.threshold, K.ratio);
    return 1;
}

void conflicts_draw(i32 height) {
    gfx_fill(0, 0, screen.width, height, COL_OUTER);
    if (!K.valid) return;
    static char c0[200], c1[260], c2[200];
    char bank[32];
    format_bytes(bank, sizeof bank, (double)K.bank);
    fmt(c0, sizeof c0, "При %d линейках чтение с конфликтами шло в %.2f раза дольше, чем без конфликтов.",
        (int)K.xs[K.points - 1], K.ratio);
    if (K.threshold > 0) {
        char at[32];
        format_bytes(at, sizeof at, K.threshold_off);
        fmt(c1, sizeof c1, "Чтение с шагом банка стало в %.1f раза дольше со смещения %s, то есть с %d линеек: они "
                           "перестали помещаться в один набор.", JUMP, at, (int)K.threshold);
    } else {
        fmt(c1, sizeof c1, "Чтение с шагом банка ни разу не стало в %.1f раза дольше. Проверьте размер банка: "
                           "объём L1 делить на число путей.", JUMP);
    }
    fmt(c2, sizeof c2, "Шаг с конфликтами %s, без конфликтов на %lu Б больше. Время обращения: наименьшее из %d.",
        bank, K.line, REPEATS);
    series lines[] = {{"С конфликтами", COL_RED, K.conflict_off, K.conflict_ns, K.points},
                      {"Без конфликтов", COL_GREEN, K.free_off, K.free_ns, K.points}};
    double right = K.free_off[K.points - 1];
    expscreen s = {.graph = {.x0 = 0, .x1 = right > 0 ? right : (double)K.bank, .y0 = 0,
                             .y1 = axis_top(K.conflict_ns, K.points), .x_kind = AXIS_BYTES,
                             .title = "Исследование конфликтов в кэш-памяти",
                             .x_label = "Смещение читаемой ячейки от начала блока", .y_label = "нс"},
                   .lines = lines, .nlines = 2, .marks = &K.threshold_off, .nmarks = K.threshold > 0,
                   .mark_name = "Начало конфликтов", .conclusions = {c0, c1, c2}};
    expscreen_draw(&s, height);
}

usize conflicts_report(u8 *out, usize cap) {
    char *s = (char *)out;
    if (!K.valid) return fmt(s, cap, "error: experiment 5 has not run yet");
    usize n = fmt(s, cap, "experiment cache_conflicts\nbank_bytes %lu\nline_bytes %lu\nconflict_threshold_lines %d\n"
                          "conflict_to_no_conflict_ratio %.3f\ntsc_ghz %.4f\n"
                          "data lines offset_conflict offset_no_conflict conflict_ns no_conflict_ns\n",
                  K.bank, K.line, (int)K.threshold, K.ratio, tsc_per_ns);
    for (usize i = 0; i < K.points && n + 64 < cap; i++) {
        n += fmt(s + n, cap - n, "%d %lu %lu %.2f %.2f\n", (int)K.xs[i], (u64)K.conflict_off[i], (u64)K.free_off[i],
                 K.conflict_ns[i], K.free_ns[i]);
    }
    return n;
}
