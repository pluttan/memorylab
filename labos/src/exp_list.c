// Experiment 2: a list against an array. The next element of a list is known only after the current
// one is read, so the loads wait for each other; the array lets the processor read ahead.
#include "exp_list.h"

#include "expscreen.h"
#include "gfx.h"
#include "measure.h"
#include "plotmath.h"

#define MAX_POINTS 500
#define REPEATS 3

typedef struct node {
    struct node *next;
    i64 val;  // 16 byte nodes, as in memorylab
} node;

static struct {
    double frag_kb[MAX_POINTS], list_us[MAX_POINTS], array_us[MAX_POINTS];
    usize points;
    u64 elements;
    double ratio;
    int valid;
} L;

static double min_us(u64 best) { return ticks_ns(best) / 1000; }

// Every element goes frag places after the previous one; a taken place moves it to the next free one
static void build_list(node *list, u8 *used, u64 n, u64 frag) {
    memset(used, 0, n);
    u64 prev = 0;
    used[0] = 1;
    for (u64 i = 1; i < n; i++) {
        u64 cur = (prev + frag) % n;
        while (used[cur]) cur = cur + 1 == n ? 0 : cur + 1;
        used[cur] = 1;
        list[prev].next = &list[cur];
        list[prev].val = (i64)(i - 1);
        prev = cur;
    }
    list[prev].next = 0;
    list[prev].val = (i64)(n - 1);
}

static u64 time_list(const node *list) {
    u64 best = ~0ull;
    for (int r = 0; r < REPEATS; r++) {
        usize q = quiet_begin();
        u64 t0 = tsc_begin();
        i64 m = list[0].val;
        for (const node *p = list; p; p = p->next) m = p->val > m ? p->val : m;
        u64 t = tsc_end() - t0;
        quiet_end(q);
        keep((u64)m);
        if (t < best) best = t;
    }
    return best;
}

static u64 time_array(const i32 *arr, u64 n) {
    u64 best = ~0ull;
    for (int r = 0; r < REPEATS; r++) {
        usize q = quiet_begin();
        u64 t0 = tsc_begin();
        i32 m = arr[0];
        for (u64 i = 0; i < n; i++) m = arr[i] > m ? arr[i] : m;
        u64 t = tsc_end() - t0;
        quiet_end(q);
        keep((u64)m);
        if (t < best) best = t;
    }
    return best;
}

int list_run(const exp_param *params) {
    u64 n = (u64)params[0].value << 20;
    u64 max_frag = (u64)params[1].value * 1024 / sizeof(node), frag_step = (u64)params[2].value * 1024 / sizeof(node);
    if (frag_step < 1) frag_step = 1;
    if (max_frag / frag_step + 1 > MAX_POINTS) frag_step = (max_frag + MAX_POINTS - 1) / MAX_POINTS;
    usize list_pages = (n * sizeof(node) + 4095) / 4096, arr_pages = (n * 4 + 4095) / 4096, used_pages = (n + 4095) / 4096;
    node *list = alloc_pages(list_pages);
    i32 *arr = alloc_pages(arr_pages);
    u8 *used = alloc_pages(used_pages);
    int ok = list && arr && used;
    if (ok) {
        for (u64 i = 0; i < n; i++) arr[i] = (i32)i;
        L.points = 0;
        L.elements = n;
        double sum_list = 0, sum_array = 0;
        u64 total = max_frag / frag_step + 1;
        for (u64 frag = 1; frag <= max_frag + 1 && L.points < MAX_POINTS; frag += frag_step) {
            progress(L.points, total);
            build_list(list, used, n, frag);
            u64 tl = time_list(list), ta = time_array(arr, n);
            L.frag_kb[L.points] = (double)(frag * sizeof(node)) / 1024;
            L.list_us[L.points] = min_us(tl);
            L.array_us[L.points] = min_us(ta);
            sum_list += L.list_us[L.points], sum_array += L.array_us[L.points];
            L.points++;
        }
        L.ratio = sum_array > 0 ? sum_list / sum_array : 0;
        L.valid = 1;
        log_line("list: %lu elements, %lu points, ratio %.1f", n, L.points, L.ratio);
    }
    if (list) BS->free_pages((efi_phys_addr)list, list_pages);
    if (arr) BS->free_pages((efi_phys_addr)arr, arr_pages);
    if (used) BS->free_pages((efi_phys_addr)used, used_pages);
    return ok;
}

void list_draw(i32 height) {
    gfx_fill(0, 0, screen.width, height, COL_OUTER);
    if (!L.valid) return;
    static char c0[160], c1[200];
    char size[32];
    format_bytes(size, sizeof size, (double)(L.elements * sizeof(node)));
    fmt(c0, sizeof c0, "Список обрабатывался в %.1f раза дольше массива.", L.ratio);
    fmt(c1, sizeof c1, "Элементов %lu М, узел списка %lu Б, весь список %s. Время каждого замера: наименьшее из %d.",
        L.elements >> 20, (u64)sizeof(node), size, REPEATS);
    series lines[] = {{"Список", COL_RED, L.frag_kb, L.list_us, L.points},
                      {"Массив", COL_GREEN, L.frag_kb, L.array_us, L.points}};
    double lo, hi;
    min_max(L.list_us, L.points, &lo, &hi);
    expscreen s = {.graph = {.x0 = 0, .x1 = L.frag_kb[L.points - 1], .y0 = 0, .y1 = hi * 1.08,
                             .title = "Сравнение эффективности ссылочных и векторных структур",
                             .x_label = "Фрагментация, КБ", .y_label = "мкс"},
                   .lines = lines, .nlines = 2, .conclusions = {c0, c1}};
    expscreen_draw(&s, height);
}

usize list_report(u8 *out, usize cap) {
    char *s = (char *)out;
    if (!L.valid) return fmt(s, cap, "error: experiment 2 has not run yet");
    usize n = fmt(s, cap, "experiment list_vs_array\nelements %lu\nnode_bytes %lu\nlist_to_array_ratio %.3f\n"
                          "tsc_ghz %.4f\ndata fragmentation_kb list_us array_us\n",
                  L.elements, (u64)sizeof(node), L.ratio, tsc_per_ns);
    for (usize i = 0; i < L.points && n + 64 < cap; i++) {
        n += fmt(s + n, cap - n, "%.3f %.2f %.2f\n", L.frag_kb[i], L.list_us[i], L.array_us[i]);
    }
    return n;
}
