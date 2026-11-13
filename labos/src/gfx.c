#include "gfx.h"

#include "font_gost.h"
#include "lib.h"

surface screen;

static efi_gop *gop;
static int swap_rb;  // the frame buffer stores R in the low byte

int gfx_init(void) {
    efi_guid guid = EFI_GOP_GUID;
    if (BS->locate_protocol(&guid, 0, (void **)&gop) != EFI_SUCCESS || !gop) {
        log_line("gfx: no graphics output protocol");
        return 0;
    }
    efi_gop_mode_info *info = gop->mode->info;
    if (info->pixel_format != EFI_PIXEL_BGR_RESERVED_8BIT && info->pixel_format != EFI_PIXEL_RGB_RESERVED_8BIT) {
        log_line("gfx: pixel format %u is not supported", info->pixel_format);
        return 0;
    }
    swap_rb = info->pixel_format == EFI_PIXEL_RGB_RESERVED_8BIT;
    screen.width = info->horizontal_resolution;
    screen.height = info->vertical_resolution;
    // Drawing goes to a back buffer: reading video memory is slow, and the network screenshot reads it
    screen.pixels = alloc_pages((screen.width * screen.height * 4 + 4095) / 4096);
    if (!screen.pixels) {
        log_line("gfx: no memory for the back buffer");
        return 0;
    }
    log_line("gfx: %ux%u, stride %u, format %u", screen.width, screen.height, info->pixels_per_scan_line,
             info->pixel_format);
    return 1;
}

void gfx_present(void) {
    u32 *fb = (u32 *)gop->mode->frame_buffer_base;
    u32 stride = gop->mode->info->pixels_per_scan_line;
    for (u32 y = 0; y < screen.height; y++) {
        u32 *src = screen.pixels + (usize)y * screen.width;
        u32 *dst = fb + (usize)y * stride;
        if (!swap_rb) {
            memcpy(dst, src, screen.width * 4);
            continue;
        }
        for (u32 x = 0; x < screen.width; x++) {
            u32 c = src[x];
            dst[x] = (c & 0x00ff00) | ((c >> 16) & 0xff) | ((c & 0xff) << 16);
        }
    }
}

// ==========================
// ===  Shapes            ===
// ==========================

static inline void plot_pixel(i32 x, i32 y, u32 rgb) {
    if (x >= 0 && y >= 0 && (u32)x < screen.width && (u32)y < screen.height) {
        screen.pixels[(usize)y * screen.width + x] = rgb;
    }
}

void gfx_fill(i32 x, i32 y, i32 w, i32 h, u32 rgb) {
    i32 x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (i32)screen.width) x1 = screen.width;
    if (y1 > (i32)screen.height) y1 = screen.height;
    for (i32 j = y; j < y1; j++) {
        u32 *row = screen.pixels + (usize)j * screen.width;
        for (i32 i = x; i < x1; i++) row[i] = rgb;
    }
}

void gfx_dot(i32 x, i32 y, i32 radius, u32 rgb) {
    for (i32 j = -radius; j <= radius; j++) {
        for (i32 i = -radius; i <= radius; i++) {
            if (i * i + j * j <= radius * radius) plot_pixel(x + i, y + j, rgb);
        }
    }
}

void gfx_line(i32 x0, i32 y0, i32 x1, i32 y1, u32 rgb, int thickness) {
    i32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
    i32 dy = y1 > y0 ? y0 - y1 : y1 - y0;
    i32 sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    i32 err = dx + dy;
    i32 r = thickness / 2;
    for (;;) {
        if (r) {
            gfx_fill(x0 - r, y0 - r, thickness, thickness, rgb);
        } else {
            plot_pixel(x0, y0, rgb);
        }
        if (x0 == x1 && y0 == y1) break;
        i32 e2 = 2 * err;
        if (e2 >= dy) err += dy, x0 += sx;
        if (e2 <= dx) err += dx, y0 += sy;
    }
}

// ==========================
// ===  Text              ===
// ==========================

static int find_glyph(u32 code) {
    int lo = 0, hi = FONT_GLYPHS - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (font_glyphs[mid].code == code) return mid;
        if (font_glyphs[mid].code < code) lo = mid + 1;
        else hi = mid - 1;
    }
    return code == '?' ? -1 : find_glyph('?');
}

static u32 blend(u32 bg, u32 fg, u32 a) {
    u32 r = (((fg >> 16) & 0xff) * a + ((bg >> 16) & 0xff) * (255 - a)) / 255;
    u32 g = (((fg >> 8) & 0xff) * a + ((bg >> 8) & 0xff) * (255 - a)) / 255;
    u32 b = ((fg & 0xff) * a + (bg & 0xff) * (255 - a)) / 255;
    return (r << 16) | (g << 8) | b;
}

i32 gfx_text_n(i32 x, i32 y, const char *utf8, usize length, u32 rgb) {
    i32 start = x;
    const char *end = utf8 + length;
    while (utf8 < end && *utf8) {
        int g = find_glyph(utf8_next(&utf8));
        if (g < 0) continue;
        const u8 *alpha = font_alpha + font_glyphs[g].offset;
        i32 gx = x + font_glyphs[g].left, gy = y + font_glyphs[g].top;
        for (i32 j = 0; j < font_glyphs[g].h; j++) {
            for (i32 i = 0; i < font_glyphs[g].w; i++) {
                u32 a = alpha[j * font_glyphs[g].w + i];
                i32 px = gx + i, py = gy + j;
                if (!a || px < 0 || py < 0 || (u32)px >= screen.width || (u32)py >= screen.height) continue;
                u32 *p = screen.pixels + (usize)py * screen.width + px;
                *p = a == 255 ? rgb : blend(*p, rgb, a);
            }
        }
        x += font_glyphs[g].advance;
    }
    return x - start;
}

i32 gfx_text_width_n(const char *utf8, usize length) {
    i32 w = 0;
    const char *end = utf8 + length;
    while (utf8 < end && *utf8) {
        int g = find_glyph(utf8_next(&utf8));
        if (g >= 0) w += font_glyphs[g].advance;
    }
    return w;
}

i32 gfx_text(i32 x, i32 y, const char *utf8, u32 rgb) { return gfx_text_n(x, y, utf8, strlen(utf8), rgb); }

i32 gfx_text_width(const char *utf8) { return gfx_text_width_n(utf8, strlen(utf8)); }

i32 gfx_line_height(void) { return FONT_HEIGHT; }
