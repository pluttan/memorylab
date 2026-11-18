#include "plot.h"

#include "gfx.h"
#include "lib.h"
#include "plotmath.h"

#define PAD 12
#define TICK 6

static double fx_of(const plot *p, double v) { return axis_pos(v, p->x0, p->x1, p->log_x, p->fx, p->fx + p->fw); }
static double fy_of(const plot *p, double v) {
    if (p->log_y && v < p->y0) v = p->y0;
    return axis_pos(v, p->y0, p->y1, p->log_y, p->fy + p->fh, p->fy);
}
static i32 to_x(const plot *p, double v) { return (i32)fx_of(p, v); }
static i32 to_y(const plot *p, double v) { return (i32)fy_of(p, v); }

static void tick_label(char *out, usize size, double v, double step, axis_kind kind) {
    if (kind == AXIS_BYTES) {
        format_bytes(out, (unsigned)size, v);
    } else {
        format_number(out, (unsigned)size, v, step_decimals(step));
    }
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
    double step = p->x_kind == AXIS_BYTES ? nice_step_bytes(p->x0, p->x1, p->fw / 120)
                                          : nice_step(p->x0, p->x1, p->fw / 110);
    i64 first = (i64)(p->x0 / step);
    for (double v = first * step; v <= p->x1 + step * 1e-6; v += step) {
        if (v < p->x0 - step * 1e-6) continue;
        i32 x = to_x(p, v);
        tick_label(text, sizeof text, v, step, p->x_kind);
        gfx_line(x, p->fy, x, p->fy + p->fh, COL_GRID, 1);
        gfx_line(x, p->fy + p->fh, x, p->fy + p->fh + TICK, COL_AXIS, 1);
        // The last label would run off the right edge
        i32 w = gfx_text_width(text), left = x - w / 2;
        if (left + w > p->x + p->w - 4) left = p->x + p->w - 4 - w;
        gfx_text(left, label_y, text, COL_SUBTEXT);
    }
}

static void y_tick(const plot *p, double v, const char *text) {
    i32 y = to_y(p, v);
    gfx_line(p->fx, y, p->fx + p->fw, y, COL_GRID, 1);
    gfx_line(p->fx - TICK, y, p->fx, y, COL_AXIS, 1);
    gfx_text(p->fx - TICK - 4 - gfx_text_width(text), y - gfx_line_height() / 2, text, COL_SUBTEXT);
}

static void y_ticks(const plot *p) {
    char text[32];
    if (p->log_y) {
        for (double v = 1; v <= p->y1 * 1.0001; v *= 10) {
            if (v < p->y0) continue;
            format_count(text, sizeof text, (u64)v);
            y_tick(p, v, text);
        }
        return;
    }
    double step = nice_step(p->y0, p->y1, p->fh / 60);
    i64 first = (i64)(p->y0 / step);
    for (double v = first * step; v <= p->y1 + step * 1e-6; v += step) {
        if (v < p->y0 - step * 1e-6) continue;
        tick_label(text, sizeof text, v, step, AXIS_NUMBER);
        y_tick(p, v, text);
    }
}

void plot_frame(plot *p) {
    i32 lh = gfx_line_height();
    gfx_fill(p->x, p->y, p->w, p->h, COL_OUTER);
    if (p->x1 <= p->x0) p->x1 = p->x0 + 1;
    if (p->y1 <= p->y0) p->y1 = p->y0 + 1;
    if (p->log_y && p->y0 <= 0) p->y0 = 0.8;
    // The widest y label decides the left margin
    char text[32];
    double step = nice_step(p->y0, p->y1, 8);
    if (p->log_y) {
        format_count(text, sizeof text, (u64)p->y1);
    } else {
        tick_label(text, sizeof text, p->y1, step, AXIS_NUMBER);
    }
    i32 left = gfx_text_width(text) + TICK + 4 + PAD + lh + 4;
    p->fx = p->x + left;
    p->fy = p->y + PAD + lh + 6 + (p->legend_row ? lh + 4 : 0);
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

static void clip_field(const plot *p) { gfx_clip(p->fx, p->fy, p->fw + 1, p->fh + 1); }

void plot_line(const plot *p, const double *xs, const double *ys, usize n, u32 color) {
    clip_field(p);
    for (usize i = 1; i < n; i++) {
        gfx_line_aa(fx_of(p, xs[i - 1]), fy_of(p, ys[i - 1]), fx_of(p, xs[i]), fy_of(p, ys[i]), color, 2.2);
    }
    gfx_clip_reset();
}

void plot_dots(const plot *p, const double *xs, const double *ys, usize n, u32 color, double radius) {
    clip_field(p);
    for (usize i = 0; i < n; i++) gfx_dot_aa(fx_of(p, xs[i]), fy_of(p, ys[i]), radius, color);
    gfx_clip_reset();
}

void plot_hline(const plot *p, double y, u32 color) {
    if (y < p->y0 || y > p->y1) return;
    clip_field(p);
    gfx_line_aa(p->fx, fy_of(p, y), p->fx + p->fw, fy_of(p, y), color, 2);
    gfx_clip_reset();
}

void plot_vline(const plot *p, double x, u32 color) {
    if (x < p->x0 || x > p->x1) return;
    gfx_vline_dashed(to_x(p, x), p->fy, p->fy + p->fh, color, 6);
}

void plot_bars(const plot *p, const double *xs, double width, const double *ys, usize n, u32 color) {
    for (usize i = 0; i < n; i++) {
        if (ys[i] <= (p->log_y ? p->y0 : 0)) continue;
        i32 left = to_x(p, xs[i]), right = to_x(p, xs[i] + width), top = to_y(p, ys[i]);
        if (right - left > 2) right--;  // a hairline gap between neighbours
        if (left < p->fx) left = p->fx;
        if (right > p->fx + p->fw) right = p->fx + p->fw;
        if (top < p->fy) top = p->fy;
        gfx_fill(left, top, right - left > 0 ? right - left : 1, p->fy + p->fh - top, color);
    }
}

void plot_note(const plot *p, const char *text, u32 color) { gfx_text(p->fx + 12, p->fy + 8, text, color); }

void plot_legend(const plot *p, const legend_item *items, usize n) {
    i32 x = p->fx + p->fw, y = p->y + PAD + (p->legend_row ? gfx_line_height() + 4 : 0);
    // Too wide for the row next to the y unit: the legend goes inside the field, at its top right
    i32 total = 0;
    for (usize i = 0; i < n; i++) total += gfx_text_width(items[i].name) + 42;
    if (p->legend_row && x - total < p->fx + gfx_text_width(p->y_label ? p->y_label : "") / 2 + 12) {
        x -= 10;
        y = p->fy + 8;
    }
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

double axis_top(const double *v, usize n) {
    double lo, hi;
    min_max(v, n, &lo, &hi);
    return hi > 0 ? hi * 1.08 : 1;
}
