// Experiment 7: QuickSort compares the elements in pairs; Radix-Counting Sort spreads them over the
// eight bytes of the key without comparisons. The optimized radix sort (PCLAB «ERadix») counts all eight
// bytes in one pass over the array. The data are 64 bit splitmix64 numbers: every bit is random.
#include "exp_sort.h"

#include "expscreen.h"
#include "gfx.h"
#include "measure.h"
#include "plotmath.h"

#define MAX_POINTS 512
#define ALGORITHMS 3

static const char *const NAME[ALGORITHMS] = {"QuickSort", "Radix-Counting Sort", "Radix-Counting Sort, оптимизированный"};
static const u32 COLOR[ALGORITHMS] = {COL_RED, COL_BLUE, COL_GREEN};

static struct {
    double xs[MAX_POINTS], ms[ALGORITHMS][MAX_POINTS];
    u64 elements[MAX_POINTS];
    usize points;
    double total[ALGORITHMS];
    int sorted;  // every result came out in order
    int valid;
} T;

static void quick_sort(u64 *a, i64 lo_in, i64 hi_in) {
    i64 lo = lo_in, hi = hi_in;
    u64 mid = a[(lo + hi) >> 1];
    do {
        while (a[lo] < mid) lo++;
        while (a[hi] > mid) hi--;
        if (lo <= hi) {
            u64 t = a[lo];
            a[lo] = a[hi], a[hi] = t;
            lo++, hi--;
        }
    } while (lo < hi);
    if (hi > lo_in) quick_sort(a, lo_in, hi);
    if (lo < hi_in) quick_sort(a, lo, hi_in);
}

// Eight passes, one per byte of the key; the result is back in `data` after the even number of passes
static void radix_sort(u64 *data, u64 *tmp, u64 n) {
    static u32 count[256];
    u64 *src = data, *dst = tmp;
    for (int pass = 0; pass < 8; pass++) {
        const u8 *bytes = (const u8 *)src;
        for (int j = 0; j < 256; j++) count[j] = 0;
        for (u64 j = 0; j < n; j++) count[bytes[pass + j * 8]]++;
        for (int j = 1; j < 256; j++) count[j] += count[j - 1];
        for (i64 j = (i64)n - 1; j >= 0; j--) dst[--count[bytes[pass + (u64)j * 8]]] = src[j];
        u64 *t = src;
        src = dst, dst = t;
    }
}

// All eight byte counts in one pass over the array: eight counters side by side
static void radix_sort_opt(u64 *data, u64 *tmp, u64 n) {
    static u32 count[256 * 8];
    for (int i = 0; i < 256 * 8; i++) count[i] = 0;
    const u8 *bytes = (const u8 *)data;
    for (u64 k = 0, idx = 0; k < n; k++, idx += 8) {
        count[bytes[idx + 0] * 8 + 0]++;
        count[bytes[idx + 1] * 8 + 1]++;
        count[bytes[idx + 2] * 8 + 2]++;
        count[bytes[idx + 3] * 8 + 3]++;
        count[bytes[idx + 4] * 8 + 4]++;
        count[bytes[idx + 5] * 8 + 5]++;
        count[bytes[idx + 6] * 8 + 6]++;
        count[bytes[idx + 7] * 8 + 7]++;
    }
    for (int j = 1; j < 256; j++) {
        for (int p = 0; p < 8; p++) count[j * 8 + p] += count[(j - 1) * 8 + p];
    }
    u64 *src = data, *dst = tmp;
    for (int pass = 0; pass < 8; pass++) {
        const u8 *b = (const u8 *)src;
        for (i64 j = (i64)n - 1; j >= 0; j--) dst[--count[b[(u64)j * 8 + pass] * 8 + pass]] = src[j];
        u64 *t = src;
        src = dst, dst = t;
    }
}

static u64 splitmix(u64 *state) {
    u64 z = (*state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static int in_order(const u64 *a, u64 n) {
    for (u64 i = 1; i < n; i++) {
        if (a[i - 1] > a[i]) return 0;
    }
    return 1;
}

int sort_run(const exp_param *params) {
    u64 max_n = (u64)params[0].value << 20, step = (u64)params[1].value << 10;
    if (max_n / step > MAX_POINTS) step = (max_n + MAX_POINTS - 1) / MAX_POINTS;
    usize pages = (usize)((max_n * 8 + 4095) / 4096);
    u64 *buf[5];
    int ok = 1;
    for (int i = 0; i < 5; i++) {
        buf[i] = alloc_pages(pages);
        ok &= buf[i] != 0;
    }
    if (ok) {
        // Every buffer touched before the timing, as on the lab computers under an operating system
        for (int i = 0; i < 5; i++) memset(buf[i], 0, pages * 4096);
        u64 *quick = buf[0], *radix = buf[1], *tmp = buf[2], *radix_opt = buf[3], *tmp_opt = buf[4];
        u64 state = 0x9E3779B97F4A7C15ull;
        T.points = 0;
        T.sorted = 1;
        for (int a = 0; a < ALGORITHMS; a++) T.total[a] = 0;
        u64 total_points = max_n / step;
        for (u64 n = step; n <= max_n && T.points < MAX_POINTS; n += step) {
            progress(T.points, total_points);
            for (u64 i = 0; i < n; i++) quick[i] = radix[i] = radix_opt[i] = splitmix(&state);
            usize q = quiet_begin();
            u64 t0 = tsc_begin();
            quick_sort(quick, 0, (i64)n - 1);
            u64 t1 = tsc_end();
            radix_sort(radix, tmp, n);
            u64 t2 = tsc_end();
            radix_sort_opt(radix_opt, tmp_opt, n);
            u64 t3 = tsc_end();
            quiet_end(q);
            T.sorted &= in_order(quick, n) && in_order(radix, n) && in_order(radix_opt, n);
            T.elements[T.points] = n;
            T.xs[T.points] = (double)n / (1 << 20);
            T.ms[0][T.points] = ticks_ns(t1 - t0) / 1e6;
            T.ms[1][T.points] = ticks_ns(t2 - t1) / 1e6;
            T.ms[2][T.points] = ticks_ns(t3 - t2) / 1e6;
            for (int a = 0; a < ALGORITHMS; a++) T.total[a] += T.ms[a][T.points];
            T.points++;
        }
        T.valid = T.points > 0;
        log_line("sort: %lu points up to %lu, totals %.1f %.1f %.1f ms, sorted %d", T.points, max_n, T.total[0],
                 T.total[1], T.total[2], T.sorted);
    }
    for (int i = 0; i < 5; i++) {
        if (buf[i]) BS->free_pages((efi_phys_addr)buf[i], pages);
    }
    return ok && T.valid;
}

static double ratio(double a, double b) { return b > 0 ? a / b : 0; }

void sort_draw(i32 height) {
    gfx_fill(0, 0, screen.width, height, COL_OUTER);
    if (!T.valid) return;
    static char c0[220], c1[200], c2[200];
    fmt(c0, sizeof c0, "QuickSort работал в %.2f раза дольше Radix-Counting Sort и в %.2f раза дольше "
                       "оптимизированного варианта.", ratio(T.total[0], T.total[1]), ratio(T.total[0], T.total[2]));
    fmt(c1, sizeof c1, "Оптимизированный Radix-Counting Sort быстрее обычного в %.2f раза.", ratio(T.total[1], T.total[2]));
    fmt(c2, sizeof c2, "Массивов %lu, до %lu М 64-разрядных элементов.%s", T.points, T.elements[T.points - 1] >> 20,
        T.sorted ? "" : " Ошибка: результат не упорядочен.");
    series lines[ALGORITHMS];
    double hi = 0;
    for (int a = 0; a < ALGORITHMS; a++) {
        lines[a] = (series){NAME[a], COLOR[a], T.xs, T.ms[a], T.points};
        double t = axis_top(T.ms[a], T.points);
        if (t > hi) hi = t;
    }
    expscreen s = {.graph = {.x0 = 0, .x1 = T.xs[T.points - 1], .y0 = 0, .y1 = hi,
                             .title = "Сравнение алгоритмов сортировки",
                             .x_label = "Число элементов, М", .y_label = "мс"},
                   .lines = lines, .nlines = ALGORITHMS, .conclusions = {c0, c1, c2}};
    expscreen_draw(&s, height);
}

usize sort_report(u8 *out, usize cap) {
    char *s = (char *)out;
    if (!T.valid) return fmt(s, cap, "error: experiment 7 has not run yet");
    usize n = fmt(s, cap, "experiment sorting_algorithms\npoints %lu\nsorted %d\ntotal_quicksort_ms %.3f\n"
                          "total_radix_ms %.3f\ntotal_radix_opt_ms %.3f\ntsc_ghz %.4f\n"
                          "data elements quicksort_ms radix_ms radix_opt_ms\n",
                  T.points, T.sorted, T.total[0], T.total[1], T.total[2], tsc_per_ns);
    for (usize i = 0; i < T.points && n + 80 < cap; i++) {
        n += fmt(s + n, cap - n, "%lu %.3f %.3f %.3f\n", T.elements[i], T.ms[0][i], T.ms[1][i], T.ms[2][i]);
    }
    return n;
}
