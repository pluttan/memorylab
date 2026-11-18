// The result screen most experiments share: a graph of a few lines and the conclusions under it.
#ifndef LABOS_EXPSCREEN_H
#define LABOS_EXPSCREEN_H

#include "plot.h"

typedef struct {
    const char *name;
    u32 color;
    const double *xs, *ys;
    usize n;
} series;

#define MAX_CONCLUSIONS 4

typedef struct {
    plot graph;  // title, axes and ranges; the box is set by expscreen_draw
    const series *lines;
    usize nlines;
    const double *marks;  // dashed vertical marks at these x values
    usize nmarks;
    const char *mark_name;  // legend entry of the marks, or 0
    const char *conclusions[MAX_CONCLUSIONS];  // the first in the text colour, the rest dimmer
} expscreen;

void expscreen_draw(expscreen *s, i32 height);

#endif
