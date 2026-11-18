// Experiment 4: several data streams read from separate arrays and from one array where the elements of
// all streams stand next to each other. Separate arrays need a DRAM row and a prefetch stream per array;
// one array gives the elements of all streams in the same lines. labos reads with interrupts off and
// keeps the smallest of three times.
#include "exp_streams.h"

#include "exp_cpuinfo.h"
#include "expscreen.h"
#include "gfx.h"
#include "measure.h"
#include "plotmath.h"

#define MAX_STREAMS 128
#define REPEATS 3

static struct {
    double xs[MAX_STREAMS], separate_us[MAX_STREAMS], interleaved_us[MAX_STREAMS];
    usize points;
    u64 array_bytes, llc_bytes;
    double dram_from;  // the first number of streams whose data does not fit into the last cache, or 0
    double ratio;
    int valid;
} S;

// The sum stays in a register: a memory sum would add a store to every read
#define HOLD(x) __asm__ volatile("" : "+r"(x))

static u64 time_separate(i32 *const *arrays, u64 elements, int streams) {
    u64 best = ~0ull;
    for (int r = 0; r < REPEATS; r++) {
        usize q = quiet_begin();
        u64 t0 = tsc_begin();
        i32 x = 0;
        for (u64 a = 0; a < elements; a++) {
            for (int s = 0; s < streams; s++) {
                x += arrays[s][a];
                HOLD(x);
            }
        }
        u64 t = tsc_end() - t0;
        quiet_end(q);
        keep((u64)x);
        if (t < best) best = t;
    }
    return best;
}

static u64 time_interleaved(const i32 *array, u64 elements, int streams) {
    u64 best = ~0ull;
    u64 total = elements * (u64)streams;
    for (int r = 0; r < REPEATS; r++) {
        usize q = quiet_begin();
        u64 t0 = tsc_begin();
        i32 x = 0;
        for (u64 a = 0; a < total; a += (u64)streams) {
            for (int s = 0; s < streams; s++) {
                x += array[a + (u64)s];
                HOLD(x);
            }
        }
        u64 t = tsc_end() - t0;
        quiet_end(q);
        keep((u64)x);
        if (t < best) best = t;
    }
    return best;
}

int streams_run(const exp_param *params) {
    S.array_bytes = (u64)params[0].value << 20;
    int max_streams = (int)params[1].value;
    u64 elements = S.array_bytes / sizeof(i32);
    usize pages = (usize)(S.array_bytes * (u64)max_streams / 4096);
    u8 *separate = alloc_pages(pages);
    i32 *interleaved = alloc_pages(pages);
    int ok = separate && interleaved;
    if (ok) {
        // Every array begins on its own page, as separate allocations would
        static i32 *arrays[MAX_STREAMS];
        for (int s = 0; s < max_streams; s++) arrays[s] = (i32 *)(separate + S.array_bytes * (u64)s);
        memset(separate, 0, (usize)pages * 4096);
        memset(interleaved, 0, (usize)pages * 4096);
        S.llc_bytes = cpu_cache_bytes(0);
        S.dram_from = S.llc_bytes ? (double)(S.llc_bytes / S.array_bytes + 1) : 0;
        if (S.dram_from > max_streams) S.dram_from = 0;
        double sum_separate = 0, sum_interleaved = 0;
        S.points = 0;
        for (int streams = 1; streams <= max_streams; streams++) {
            progress((u64)streams - 1, (u64)max_streams);
            S.xs[S.points] = streams;
            S.separate_us[S.points] = ticks_ns(time_separate(arrays, elements, streams)) / 1000;
            S.interleaved_us[S.points] = ticks_ns(time_interleaved(interleaved, elements, streams)) / 1000;
            sum_separate += S.separate_us[S.points], sum_interleaved += S.interleaved_us[S.points];
            S.points++;
        }
        S.ratio = sum_interleaved > 0 ? sum_separate / sum_interleaved : 0;
        S.valid = 1;
        log_line("streams: %d streams of %lu MB, ratio %.2f, LLC %lu", max_streams, S.array_bytes >> 20, S.ratio,
                 S.llc_bytes);
    }
    if (separate) BS->free_pages((efi_phys_addr)separate, pages);
    if (interleaved) BS->free_pages((efi_phys_addr)interleaved, pages);
    return ok;
}

void streams_draw(i32 height) {
    gfx_fill(0, 0, screen.width, height, COL_OUTER);
    if (!S.valid) return;
    static char c0[200], c1[220];
    char llc[32];
    format_bytes(llc, sizeof llc, (double)S.llc_bytes);
    fmt(c0, sizeof c0, "Чтение из отдельных массивов шло в %.2f раза дольше, чем из одного массива.", S.ratio);
    if (S.dram_from > 0) {
        fmt(c1, sizeof c1, "Массивы по %lu МБ. С %d потоков данные больше кэша последнего уровня (%s) и идут из "
                           "оперативной памяти.", S.array_bytes >> 20, (int)S.dram_from, llc);
    } else {
        fmt(c1, sizeof c1, "Массивы по %lu МБ. Кэш последнего уровня %s. Время каждого замера: наименьшее из %d.",
            S.array_bytes >> 20, llc, REPEATS);
    }
    series lines[] = {{"Отдельные массивы", COL_RED, S.xs, S.separate_us, S.points},
                      {"Один массив", COL_GREEN, S.xs, S.interleaved_us, S.points}};
    expscreen s = {.graph = {.x0 = 1, .x1 = S.points > 1 ? S.xs[S.points - 1] : 2, .y0 = 0,
                             .y1 = axis_top(S.separate_us, S.points),
                             .title = "Исследование оптимизации чтения оперативной памяти",
                             .x_label = "Число потоков", .y_label = "мкс"},
                   .lines = lines, .nlines = 2, .marks = &S.dram_from, .nmarks = S.dram_from > 0,
                   .mark_name = "Данные больше кэша", .conclusions = {c0, c1}};
    expscreen_draw(&s, height);
}

usize streams_report(u8 *out, usize cap) {
    char *s = (char *)out;
    if (!S.valid) return fmt(s, cap, "error: experiment 4 has not run yet");
    usize n = fmt(s, cap, "experiment memory_read_optimization\narray_bytes %lu\nllc_bytes %lu\ndram_from_streams %d\n"
                          "separate_to_interleaved_ratio %.3f\ntsc_ghz %.4f\ndata streams separate_us interleaved_us\n",
                  S.array_bytes, S.llc_bytes, (int)S.dram_from, S.ratio, tsc_per_ns);
    for (usize i = 0; i < S.points && n + 64 < cap; i++) {
        n += fmt(s + n, cap - n, "%d %.2f %.2f\n", (int)S.xs[i], S.separate_us[i], S.interleaved_us[i]);
    }
    return n;
}
