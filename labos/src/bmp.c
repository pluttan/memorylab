#include "bmp.h"

#define HEADER 54

static void put16(u8 *p, u32 v) { p[0] = (u8)v, p[1] = (u8)(v >> 8); }
static void put32(u8 *p, u32 v) { p[0] = (u8)v, p[1] = (u8)(v >> 8), p[2] = (u8)(v >> 16), p[3] = (u8)(v >> 24); }

static u32 row_bytes(u32 width) { return (width * 3 + 3) & ~3u; }

usize bmp_size(u32 width, u32 height) { return HEADER + (usize)row_bytes(width) * height; }

usize bmp_encode(const u32 *pixels, u32 width, u32 height, u8 *out, usize cap) {
    usize size = bmp_size(width, height);
    if (size > cap) return 0;
    for (usize i = 0; i < HEADER; i++) out[i] = 0;
    out[0] = 'B', out[1] = 'M';
    put32(out + 2, (u32)size);
    put32(out + 10, HEADER);
    put32(out + 14, 40);  // BITMAPINFOHEADER
    put32(out + 18, width);
    put32(out + 22, height);  // positive: rows go from the bottom
    put16(out + 26, 1);
    put16(out + 28, 24);
    put32(out + 34, (u32)(size - HEADER));
    put32(out + 38, 3780);  // 96 dpi
    put32(out + 42, 3780);
    u32 stride = row_bytes(width);
    for (u32 y = 0; y < height; y++) {
        u8 *row = out + HEADER + (usize)(height - 1 - y) * stride;
        const u32 *src = pixels + (usize)y * width;
        for (u32 x = 0; x < width; x++) {
            row[3 * x] = (u8)src[x];
            row[3 * x + 1] = (u8)(src[x] >> 8);
            row[3 * x + 2] = (u8)(src[x] >> 16);
        }
        for (u32 pad = width * 3; pad < stride; pad++) row[pad] = 0;
    }
    return size;
}
