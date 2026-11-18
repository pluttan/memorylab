#include "expscreen.h"

#include "gfx.h"
#include "lib.h"
#include "widgets.h"

#define MAX_LEGEND 6
#define TEXT_MAX 320

// Russian decimal comma, as on the axes: 1.64 becomes 1,64
static void decimal_commas(char *dst, const char *src) {
    usize i = 0;
    for (; src[i] && i + 1 < TEXT_MAX; i++) {
        int number = src[i] == '.' && i > 0 && src[i - 1] >= '0' && src[i - 1] <= '9' && src[i + 1] >= '0' &&
                     src[i + 1] <= '9';
        dst[i] = number ? ',' : src[i];
    }
    dst[i] = 0;
}

void expscreen_draw(expscreen *s, i32 height) {
    usize ntext = 0;
    static char text[MAX_CONCLUSIONS][TEXT_MAX];
    while (ntext < MAX_CONCLUSIONS && s->conclusions[ntext]) {
        decimal_commas(text[ntext], s->conclusions[ntext]);
        ntext++;
    }
    // Long conclusions wrap: measure them first
    i32 text_w = (i32)screen.width - 96, text_h = 16;
    gfx_dry = 1;
    for (usize i = 0; i < ntext; i++) text_h += wrap_text(48, 0, text_w, text[i], COL_TEXT) + 2;
    gfx_dry = 0;
    gfx_fill(0, 0, screen.width, height, COL_OUTER);
    s->graph.x = 0, s->graph.y = 0, s->graph.w = (i32)screen.width, s->graph.h = height - text_h;
    s->graph.legend_row = 1;
    plot_frame(&s->graph);
    for (usize m = 0; m < s->nmarks; m++) plot_vline(&s->graph, s->marks[m], COL_AXIS);
    for (usize i = 0; i < s->nlines; i++) plot_line(&s->graph, s->lines[i].xs, s->lines[i].ys, s->lines[i].n, s->lines[i].color);
    legend_item items[MAX_LEGEND];
    usize n = 0;
    for (usize i = 0; i < s->nlines && n < MAX_LEGEND; i++) items[n++] = (legend_item){s->lines[i].name, s->lines[i].color};
    if (s->nmarks && s->mark_name && n < MAX_LEGEND) items[n++] = (legend_item){s->mark_name, COL_AXIS};
    plot_legend(&s->graph, items, n);
    i32 y = s->graph.h + 8;
    for (usize i = 0; i < ntext; i++) y += wrap_text(48, y, text_w, text[i], i ? COL_SUBTEXT : COL_TEXT) + 2;
}
