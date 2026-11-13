// Text blocks and tables on top of gfx.
#ifndef LABOS_WIDGETS_H
#define LABOS_WIDGETS_H

#include "efi.h"

// Breaks the text at spaces to fit the width; returns the height it takes. Draws nothing when rgb is 0
i32 wrap_text(i32 x, i32 y, i32 width, const char *text, u32 rgb);

// The same for text with emphasis: \x01 switches to the accent colour, \x02 back to the base one
i32 wrap_rich(i32 x, i32 y, i32 width, const char *text, u32 base, u32 accent);

// A header row and nrows rows of ncols cells (row by row); columns take the width of their widest cell.
// Returns the height of the table
i32 draw_table(i32 x, i32 y, const char *const *headers, int ncols, const char *const *cells, int nrows);

// A table that fits the width: long cells wrap. rows includes the header row (cells row by row)
i32 draw_table_wrap(i32 x, i32 y, i32 width, int ncols, const char *const *cells, int rows);

#endif
