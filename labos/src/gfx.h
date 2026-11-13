// Drawing into a back buffer (0x00RRGGBB) and copying it to the UEFI frame buffer.
#ifndef LABOS_GFX_H
#define LABOS_GFX_H

#include "efi.h"

// Catppuccin Mocha; the outer background and the plot field follow the house figure style
#define COL_OUTER 0x171717
#define COL_FIELD 0x040404
#define COL_TEXT 0xcdd6f4
#define COL_SUBTEXT 0xa6adc8
#define COL_GRID 0x313244
#define COL_AXIS 0x6c7086
#define COL_RED 0xf38ba8
#define COL_GREEN 0xa6e3a1
#define COL_MAUVE 0xcba6f7
#define COL_BLUE 0x89b4fa
#define COL_PEACH 0xfab387
#define COL_TEAL 0x94e2d5

typedef struct {
    u32 *pixels;
    u32 width, height;
} surface;

extern surface screen;

int gfx_init(void);
void gfx_present(void);

void gfx_fill(i32 x, i32 y, i32 w, i32 h, u32 rgb);
void gfx_line(i32 x0, i32 y0, i32 x1, i32 y1, u32 rgb, int thickness);
void gfx_dot(i32 x, i32 y, i32 radius, u32 rgb);

// y is the top of the text line; returns the width in pixels
i32 gfx_text(i32 x, i32 y, const char *utf8, u32 rgb);
i32 gfx_text_width(const char *utf8);
i32 gfx_line_height(void);

#endif
