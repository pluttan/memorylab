#include "cachesteps.h"

#define SPAN 4    // half an octave at eight points per doubling
#define OCTAVE 8
#define MAX_POINTS 4096

static u64 round_up(u64 v, u64 line) { return (v + line - 1) / line * line; }

usize cache_block_sizes(u64 step, u64 max_bytes, u64 line, u64 *sizes, usize cap) {
    usize n = 0;
    for (u64 size = round_up(step > line ? step : line, line); size <= max_bytes && n < cap;) {
        sizes[n++] = size;
        u64 geometric = (u64)((double)size * CACHE_GROWTH);
        u64 next = round_up(size + step > geometric ? size + step : geometric, line);
        size = next > size ? next : size + line;
    }
    return n;
}

usize cache_steps(const u64 *sizes, const double *time, usize n, u64 bounds[3]) {
    if (n > MAX_POINTS) n = MAX_POINTS;
    if (n <= SPAN) return 0;
    static double rise[MAX_POINTS];
    for (usize i = 0; i < n; i++) rise[i] = i + SPAN < n && time[i] > 0 ? time[i + SPAN] / time[i] : 0;
    // The three largest rises that lead their octave, kept in decreasing order
    double best_rise[3] = {0, 0, 0};
    u64 best_bound[3] = {0, 0, 0};
    usize found = 0;
    for (usize i = 0; i + SPAN < n; i++) {
        if (rise[i] < CACHE_JUMP) continue;
        int largest = 1;
        usize from = i >= OCTAVE ? i - OCTAVE : 0, to = i + OCTAVE < n - 1 ? i + OCTAVE : n - 1;
        for (usize j = from; j <= to && largest; j++) largest = !(rise[j] > rise[i] || (rise[j] == rise[i] && j < i));
        if (!largest) continue;
        usize steepest = i;
        for (usize j = i; j < i + SPAN; j++) {
            if (time[j + 1] / time[j] > time[steepest + 1] / time[steepest]) steepest = j;
        }
        usize k = found < 3 ? found++ : 3;
        while (k > 0 && best_rise[k - 1] < rise[i]) {
            if (k < 3) best_rise[k] = best_rise[k - 1], best_bound[k] = best_bound[k - 1];
            k--;
        }
        if (k < 3) best_rise[k] = rise[i], best_bound[k] = sizes[steepest];
    }
    for (usize i = 0; i < found; i++) bounds[i] = best_bound[i];
    for (usize i = 1; i < found; i++) {
        for (usize j = i; j > 0 && bounds[j - 1] > bounds[j]; j--) {
            u64 t = bounds[j];
            bounds[j] = bounds[j - 1], bounds[j - 1] = t;
        }
    }
    return found;
}
