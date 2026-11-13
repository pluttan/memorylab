// A graph on the screen: frame, ticks, labels and series in the house figure style.
#ifndef LABOS_PLOT_H
#define LABOS_PLOT_H

#include "efi.h"

typedef enum { AXIS_NUMBER, AXIS_BYTES } axis_kind;

typedef struct {
    i32 x, y, w, h;  // the whole graph box on the screen
    double x0, x1, y0, y1;
    int log_x;  // base 2 logarithmic x axis (cache sizes)
    axis_kind x_kind;
    const char *title, *x_label, *y_label;
    i32 fx, fy, fw, fh;  // the field inside the axes, set by plot_frame
} plot;

typedef struct {
    const char *name;
    u32 color;
} legend_item;

void plot_frame(plot *p);
void plot_line(const plot *p, const double *xs, const double *ys, usize n, u32 color);
void plot_dots(const plot *p, const double *xs, const double *ys, usize n, u32 color);
void plot_legend(const plot *p, const legend_item *items, usize n);

// Smallest and largest of n values, for the axis ranges
void min_max(const double *v, usize n, double *lo, double *hi);

#endif
