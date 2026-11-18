// A graph on the screen: frame, ticks, labels and series in the house figure style.
#ifndef LABOS_PLOT_H
#define LABOS_PLOT_H

#include "efi.h"

typedef enum { AXIS_NUMBER, AXIS_BYTES } axis_kind;

typedef struct {
    i32 x, y, w, h;  // the whole graph box on the screen
    double x0, x1, y0, y1;
    int log_x;  // base 2 logarithmic x axis (cache sizes)
    int log_y;  // base 10 logarithmic y axis with ticks at powers of ten; y0 must be above zero
    axis_kind x_kind;
    const char *title, *x_label, *y_label;
    int legend_row;  // a row under the title for a long legend
    i32 fx, fy, fw, fh;  // the field inside the axes, set by plot_frame
} plot;

typedef struct {
    const char *name;
    u32 color;
} legend_item;

void plot_frame(plot *p);
// Series are drawn antialiased and cut at the edges of the field
void plot_line(const plot *p, const double *xs, const double *ys, usize n, u32 color);
void plot_dots(const plot *p, const double *xs, const double *ys, usize n, u32 color, double radius);
void plot_hline(const plot *p, double y, u32 color);
void plot_vline(const plot *p, double x, u32 color);  // dashed mark across the field
// Bars of width `width` (in x units) starting at xs[i], from the bottom of the field up to ys[i]
void plot_bars(const plot *p, const double *xs, double width, const double *ys, usize n, u32 color);
void plot_legend(const plot *p, const legend_item *items, usize n);
// A short note at the top left of the field
void plot_note(const plot *p, const char *text, u32 color);

// Smallest and largest of n values, for the axis ranges
void min_max(const double *v, usize n, double *lo, double *hi);
// The axis top: the largest value with a little room above it
double axis_top(const double *v, usize n);

#endif
