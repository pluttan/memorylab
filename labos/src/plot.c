#include "plot.h"

#include "gfx.h"
#include "lib.h"
#include "plotmath.h"

#define PAD 12
#define TICK 6

static i32 to_x(const plot *p, double v) {
    return (i32)axis_pos(v, p->x0, p->x1, p->log_x, p->fx, p->fx + p->fw);
}

static i32 to_y(const plot *p, double v) {
    return (i32)axis_pos(v, p->y0, p->y1, 0, p->fy + p->fh, p->fy);
}

static void tick_label(char *out, usize size, double v, double step, axis_kind kind) {
    if (kind == AXIS_BYTES) {
        format_bytes(out, (unsigned)size, v);
        return;
    }
    int decimals = step_decimals(step);
    char spec[8];
    fmt(spec, sizeof spec, "%%.%df", decimals);
    fmt(out, size, spec, v);
}

static void x_ticks(const plot *p, i32 label_y) {
    char text[32];
    if (p->log_x) {
        // One tick per power of two, thinned so that the labels do not overlap
        double v = 1;
        while (v * 2 <= p->x0) v *= 2;
        i32 last_right = -1000000;
        for (; v <= p->x1 * 1.0001; v *= 2) {
            if (v < p->x0) continue;
            i32 x = to_x(p, v);
            tick_label(text, sizeof text, v, 1, p->x_kind);
            i32 w = gfx_text_width(text);
            gfx_line(x, p->fy, x, p->fy + p->fh, COL_GRID, 1);
            if (x - w / 2 > last_right + PAD) {
                gfx_line(x, p->fy + p->fh, x, p->fy + p->fh + TICK, COL_AXIS, 1);
                gfx_text(x - w / 2, label_y, text, COL_SUBTEXT);
                last_right = x + w / 2;
            }
        }
        return;
    }
    double step = nice_step(p->x0, p->x1, p->fw / 110);
    i64 first = (i64)(p->x0 / step);
    for (double v = first * step; v <= p->x1 + step * 1e-6; v += step) {
        if (v < p->x0 - step * 1e-6) continue;
        i32 x = to_x(p, v);
        tick_label(text, sizeof text, v, step, p->x_kind);
        gfx_line(x, p->fy, x, p->fy + p->fh, COL_GRID, 1);
        gfx_line(x, p->fy + p->fh, x, p->fy + p->fh + TICK, COL_AXIS, 1);
        gfx_text(x - gfx_text_width(text) / 2, label_y, text, COL_SUBTEXT);
    }
}

static void y_ticks(const plot *p) {
    char text[32];
    double step = nice_step(p->y0, p->y1, p->fh / 60);
    i64 first = (i64)(p->y0 / step);
    for (double v = first * step; v <= p->y1 + step * 1e-6; v += step) {
        if (v < p->y0 - step * 1e-6) continue;
        i32 y = to_y(p, v);
        tick_label(text, sizeof text, v, step, AXIS_NUMBER);
        gfx_line(p->fx, y, p->fx + p->fw, y, COL_GRID, 1);
        gfx_line(p->fx - TICK, y, p->fx, y, COL_AXIS, 1);
        gfx_text(p->fx - TICK - 4 - gfx_text_width(text), y - gfx_line_height() / 2, text, COL_SUBTEXT);
    }
}

void plot_frame(plot *p) {
    i32 lh = gfx_line_height();
    gfx_fill(p->x, p->y, p->w, p->h, COL_OUTER);
    if (p->x1 <= p->x0) p->x1 = p->x0 + 1;
    if (p->y1 <= p->y0) p->y1 = p->y0 + 1;
    // The widest y label decides the left margin
    char text[32];
    double step = nice_step(p->y0, p->y1, 8);
    tick_label(text, sizeof text, p->y1, step, AXIS_NUMBER);
    i32 left = gfx_text_width(text) + TICK + 4 + PAD + lh + 4;
    p->fx = p->x + left;
    p->fy = p->y + PAD + lh + 6;
    p->fw = p->w - left - PAD * 2;
    p->fh = p->h - (p->fy - p->y) - (TICK + lh * 2 + PAD + 8);
    gfx_fill(p->fx, p->fy, p->fw, p->fh, COL_FIELD);
    // Centred, so that the y unit at the top left of the axis stays clear of it
    if (p->title) gfx_text(p->fx + (p->fw - gfx_text_width(p->title)) / 2, p->y + PAD, p->title, COL_TEXT);
    x_ticks(p, p->fy + p->fh + TICK + 4);
    y_ticks(p);
    gfx_line(p->fx, p->fy + p->fh, p->fx + p->fw, p->fy + p->fh, COL_AXIS, 1);
    gfx_line(p->fx, p->fy, p->fx, p->fy + p->fh, COL_AXIS, 1);
    if (p->x_label) {
        gfx_text(p->fx + (p->fw - gfx_text_width(p->x_label)) / 2, p->fy + p->fh + TICK + 4 + lh + 4, p->x_label,
                 COL_TEXT);
    }
    // No rotated text: the y unit sits above the axis
    if (p->y_label) gfx_text(p->fx - gfx_text_width(p->y_label) / 2, p->fy - lh - 2, p->y_label, COL_TEXT);
}

void plot_line(const plot *p, const double *xs, const double *ys, usize n, u32 color) {
    for (usize i = 1; i < n; i++) {
        gfx_line(to_x(p, xs[i - 1]), to_y(p, ys[i - 1]), to_x(p, xs[i]), to_y(p, ys[i]), color, 2);
    }
}

void plot_dots(const plot *p, const double *xs, const double *ys, usize n, u32 color) {
    for (usize i = 0; i < n; i++) gfx_dot(to_x(p, xs[i]), to_y(p, ys[i]), 1, color);
}

void plot_legend(const plot *p, const legend_item *items, usize n) {
    i32 x = p->fx + p->fw, y = p->y + PAD;
    for (usize i = n; i-- > 0;) {
        x -= gfx_text_width(items[i].name);
        gfx_text(x, y, items[i].name, items[i].color);
        x -= 18;
        gfx_fill(x, y + gfx_line_height() / 2 - 2, 12, 4, items[i].color);
        x -= 24;
    }
}

void min_max(const double *v, usize n, double *lo, double *hi) {
    *lo = n ? v[0] : 0;
    *hi = n ? v[0] : 1;
    for (usize i = 1; i < n; i++) {
        if (v[i] < *lo) *lo = v[i];
        if (v[i] > *hi) *hi = v[i];
    }
}
