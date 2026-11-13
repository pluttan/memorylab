#include "widgets.h"

#include "gfx.h"
#include "lib.h"

#define LINE_GAP 4
#define COLUMN_GAP 32
#define MAX_COLUMNS 8

i32 wrap_text(i32 x, i32 y, i32 width, const char *text, u32 rgb) {
    i32 lh = gfx_line_height() + LINE_GAP, top = y;
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
        if (rgb) gfx_text_n(x, y, line, (usize)(fit - line), rgb);
        y += lh;
        line = fit;
    }
    return y - top;
}

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
