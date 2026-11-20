#include "film.h"

#define MAX_WIDTH 4096

static u32 get32le(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

int film_parse(const u8 *data, usize size, film_info *out) {
    static const u8 magic[8] = {'L', 'F', 'I', 'L', 'M', 0, 1, 0};
    if (size < FILM_HEADER) return 0;
    for (int i = 0; i < 8; i++) {
        if (data[i] != magic[i]) return 0;
    }
    out->width = get32le(data + 8);
    out->height = get32le(data + 12);
    out->fps = get32le(data + 16);
    out->frames = get32le(data + 20);
    out->gop = get32le(data + 24);
    out->stream = data + FILM_HEADER;
    out->length = size - FILM_HEADER;
    return out->width && out->width <= MAX_WIDTH && out->height && out->fps && out->gop;
}

usize film_index(const u8 *s, usize length, usize *offsets, usize cap) {
    usize count = 0;
    for (usize i = 0; i + 3 < length && count < cap; i++) {
        if (s[i] || s[i + 1] || s[i + 2] != 1) continue;
        if ((s[i + 3] & 0x1f) == 7) offsets[count++] = i > 0 && !s[i - 1] ? i - 1 : i;
        i += 2;
    }
    return count;
}

usize film_nal_end(const u8 *s, usize length, usize pos) {
    usize i = pos;
    while (i < length && !s[i]) i++;  // the zeros of this start code
    if (i < length) i++;              // its 0x01
    for (; i + 2 < length; i++) {
        if (s[i] || s[i + 1] || s[i + 2] != 1) continue;
        return i > pos && !s[i - 1] ? i - 1 : i;  // a 4 byte start code begins one zero earlier
    }
    return length;
}

static inline u32 clamp255(i32 v) { return v < 0 ? 0 : v > 255 ? 255 : (u32)v; }

static inline u32 pixel(i32 c, i32 d, i32 e) {
    // BT.601 limited range in 8.8 fixed point
    i32 base = 298 * c + 128;
    return clamp255((base + 409 * e) >> 8) << 16 | clamp255((base - 100 * d - 208 * e) >> 8) << 8 |
           clamp255((base + 516 * d) >> 8);
}

// Shrinking: every destination pixel averages the luma of the source pixels it covers, so a thin line
// turns paler instead of vanishing between the samples; the chroma is the one in the middle of them
static void shrink(const u8 *y, const u8 *u, const u8 *v, u32 stride, u32 src_w, u32 src_h, u32 *dst, u32 dst_pitch,
                   u32 dst_w, u32 dst_h) {
    static u32 x_lo[MAX_WIDTH], x_hi[MAX_WIDTH];
    for (u32 x = 0; x < dst_w; x++) {
        u32 lo = (u32)((u64)x * src_w / dst_w), hi = (u32)((u64)(x + 1) * src_w / dst_w);
        x_lo[x] = lo, x_hi[x] = hi > lo ? hi : lo + 1;
    }
    u32 cstride = stride / 2;
    for (u32 row = 0; row < dst_h; row++) {
        u32 y_lo = (u32)((u64)row * src_h / dst_h), y_hi = (u32)((u64)(row + 1) * src_h / dst_h);
        if (y_hi <= y_lo) y_hi = y_lo + 1;
        u32 cy = (y_lo + y_hi - 1) / 4;
        const u8 *ul = u + (usize)cy * cstride, *vl = v + (usize)cy * cstride;
        u32 *out = dst + (usize)row * dst_pitch;
        for (u32 x = 0; x < dst_w; x++) {
            u32 sum = 0, n = (x_hi[x] - x_lo[x]) * (y_hi - y_lo);
            for (u32 sy = y_lo; sy < y_hi; sy++) {
                const u8 *yl = y + (usize)sy * stride;
                for (u32 sx = x_lo[x]; sx < x_hi[x]; sx++) sum += yl[sx];
            }
            u32 cx = (x_lo[x] + x_hi[x] - 1) / 4;
            out[x] = pixel((i32)((sum + n / 2) / n) - 16, (i32)ul[cx] - 128, (i32)vl[cx] - 128);
        }
    }
}

void yuv420_to_rgb(const u8 *y, const u8 *u, const u8 *v, u32 stride, u32 src_w, u32 src_h, u32 *dst, u32 dst_pitch,
                   u32 dst_w, u32 dst_h) {
    if (dst_w > MAX_WIDTH) dst_w = MAX_WIDTH;
    if (dst_w < src_w || dst_h < src_h) {
        shrink(y, u, v, stride, src_w, src_h, dst, dst_pitch, dst_w, dst_h);
        return;
    }
    static u32 map[MAX_WIDTH];
    for (u32 x = 0; x < dst_w; x++) map[x] = (u32)((u64)x * src_w / dst_w);
    u32 cstride = stride / 2;
    for (u32 row = 0; row < dst_h; row++) {
        u32 sy = (u32)((u64)row * src_h / dst_h);
        const u8 *yl = y + (usize)sy * stride;
        const u8 *ul = u + (usize)(sy / 2) * cstride;
        const u8 *vl = v + (usize)(sy / 2) * cstride;
        u32 *out = dst + (usize)row * dst_pitch;
        for (u32 x = 0; x < dst_w; x++) {
            u32 sx = map[x];
            out[x] = pixel((i32)yl[sx] - 16, (i32)ul[sx / 2] - 128, (i32)vl[sx / 2] - 128);
        }
    }
}
