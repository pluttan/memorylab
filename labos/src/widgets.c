#include "widgets.h"

#include "gfx.h"
#include "lib.h"

#define LINE_GAP 4
#define COLUMN_GAP 32
#define CELL_GAP 24
#define MAX_COLUMNS 8

// Draws [s, end) and follows the colour switches; *color carries the colour over to the next line
static void draw_segment(i32 x, i32 y, const char *s, const char *end, u32 base, u32 accent, u32 *color) {
    const char *start = s;
    for (; s < end; s++) {
        if (*s != 1 && *s != 2) continue;
        x += gfx_text_n(x, y, start, (usize)(s - start), *color);
        *color = *s == 1 ? accent : base;
        start = s + 1;
    }
    gfx_text_n(x, y, start, (usize)(end - start), *color);
}

i32 wrap_rich(i32 x, i32 y, i32 width, const char *text, u32 base, u32 accent) {
    i32 lh = gfx_line_height() + LINE_GAP, top = y;
    u32 color = base;
    const char *line = text;
    while (*line) {
        while (*line == ' ') line++;
        // Take words while they fit; a single word wider than the box goes alone
        const char *end = line, *fit = 0;
        while (*end) {
            const char *word_end = end;
            while (*word_end == ' ') word_end++;
            while (*word_end && *word_end != ' ') word_end++;
            if (fit && gfx_text_width_n(line, (usize)(word_end - line)) > width) break;
            fit = end = word_end;
        }
        if (!fit) break;
        if (base) draw_segment(x, y, line, fit, base, accent, &color);
        y += lh;
        line = fit;
    }
    return y - top;
}

i32 wrap_text(i32 x, i32 y, i32 width, const char *text, u32 rgb) { return wrap_rich(x, y, width, text, rgb, rgb); }

i32 draw_table(i32 x, i32 y, const char *const *headers, int ncols, const char *const *cells, int nrows) {
    i32 widths[MAX_COLUMNS] = {0};
    if (ncols > MAX_COLUMNS) ncols = MAX_COLUMNS;
    for (int c = 0; c < ncols; c++) {
        widths[c] = gfx_text_width(headers[c]);
        for (int r = 0; r < nrows; r++) {
            i32 w = gfx_text_width(cells[r * ncols + c]);
            if (w > widths[c]) widths[c] = w;
        }
    }
    i32 lh = gfx_line_height(), total = 0, top = y;
    for (int c = 0; c < ncols; c++) total += widths[c] + (c ? COLUMN_GAP : 0);
    i32 cx = x;
    for (int c = 0; c < ncols; c++) {
        gfx_text(cx, y, headers[c], COL_SUBTEXT);
        cx += widths[c] + COLUMN_GAP;
    }
    y += lh + 4;
    gfx_line(x, y, x + total, y, COL_GRID, 1);
    y += 6;
    for (int r = 0; r < nrows; r++) {
        cx = x;
        for (int c = 0; c < ncols; c++) {
            gfx_text(cx, y, cells[r * ncols + c], COL_TEXT);
            cx += widths[c] + COLUMN_GAP;
        }
        y += lh + LINE_GAP;
    }
    return y - top;
}

// ==========================
// ===  Wrapping table    ===
// ==========================

static i32 longest_word(const char *s) {
    i32 best = 0;
    while (*s) {
        while (*s == ' ') s++;
        const char *start = s;
        while (*s && *s != ' ') s++;
        i32 w = gfx_text_width_n(start, (usize)(s - start));
        if (w > best) best = w;
    }
    return best;
}

i32 draw_table_wrap(i32 x, i32 y, i32 width, int ncols, const char *const *cells, int rows) {
    i32 natural[MAX_COLUMNS] = {0}, least[MAX_COLUMNS] = {0}, widths[MAX_COLUMNS];
    if (ncols > MAX_COLUMNS) ncols = MAX_COLUMNS;
    for (int c = 0; c < ncols; c++) {
        for (int r = 0; r < rows; r++) {
            const char *cell = cells[r * ncols + c];
            i32 w = gfx_text_width(cell), lw = longest_word(cell);
            if (w > natural[c]) natural[c] = w;
            if (lw > least[c]) least[c] = lw;
        }
        widths[c] = natural[c];
    }
    // Narrow the widest column that can still wrap until the table fits
    i32 room = width - CELL_GAP * (ncols - 1);
    for (;;) {
        i32 total = 0, widest = -1;
        for (int c = 0; c < ncols; c++) {
            total += widths[c];
            if (widths[c] > least[c] && (widest < 0 || widths[c] > widths[widest])) widest = c;
        }
        if (total <= room || widest < 0) break;
        i32 cut = total - room < 16 ? total - room : 16;
        widths[widest] = widths[widest] - cut < least[widest] ? least[widest] : widths[widest] - cut;
    }
    i32 total = CELL_GAP * (ncols - 1), top = y;
    for (int c = 0; c < ncols; c++) total += widths[c];
    for (int r = 0; r < rows; r++) {
        i32 cx = x, row_h = 0;
        u32 color = r == 0 ? COL_SUBTEXT : COL_TEXT;
        for (int c = 0; c < ncols; c++) {
            i32 h = wrap_rich(cx, y, widths[c], cells[r * ncols + c], color, COL_PEACH);
            if (h > row_h) row_h = h;
            cx += widths[c] + CELL_GAP;
        }
        y += row_h + 4;
        gfx_line(x, y - 2, x + total, y - 2, COL_GRID, 1);
        y += 4;
    }
    return y - top;
}
