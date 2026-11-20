// Host tests of the film container, the group of pictures index and the colour conversion.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "film.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

static void put32le(u8 *p, u32 v) { p[0] = (u8)v, p[1] = (u8)(v >> 8), p[2] = (u8)(v >> 16), p[3] = (u8)(v >> 24); }

static void test_parse_and_index(void) {
    // Header, then SPS (4 byte start code), PPS, IDR slice, P slice, SPS (3 byte start code), PPS, IDR
    u8 data[128] = {'L', 'F', 'I', 'L', 'M', 0, 1, 0};
    u32 fields[5] = {1280, 720, 30, 3135, 60};
    for (int i = 0; i < 5; i++) put32le(data + 8 + 4 * i, fields[i]);
    const u8 stream[] = {0, 0, 0, 1, 0x67, 0xaa, 0, 0, 1, 0x68, 0xbb, 0, 0, 1, 0x65, 0x11, 0x22,
                         0, 0, 1, 0x41, 0x33, 0, 0, 1, 0x67, 0xcc, 0, 0, 1, 0x68, 0, 0, 1, 0x65};
    memcpy(data + FILM_HEADER, stream, sizeof stream);
    film_info f;
    CHECK(film_parse(data, FILM_HEADER + sizeof stream, &f));
    CHECK(f.width == 1280 && f.height == 720 && f.fps == 30 && f.frames == 3135 && f.gop == 60);
    CHECK(f.length == sizeof stream && f.stream == data + FILM_HEADER);
    usize offsets[4];
    CHECK(film_index(f.stream, f.length, offsets, 4) == 2);
    CHECK(offsets[0] == 0);   // the 4 byte start code begins at the first zero
    CHECK(offsets[1] == 22);  // the 3 byte one at its own first zero
    // NAL boundaries: SPS 0..5, PPS 6..10, IDR 11..16, P slice 17..21, SPS 22..
    CHECK(film_nal_end(f.stream, f.length, 0) == 6);
    CHECK(film_nal_end(f.stream, f.length, 6) == 11);
    CHECK(film_nal_end(f.stream, f.length, 11) == 17);
    CHECK(film_nal_end(f.stream, f.length, 17) == 22);
    CHECK(film_nal_end(f.stream, f.length, 31) == f.length);  // the last NAL runs to the end
    data[3] = 'X';
    CHECK(!film_parse(data, FILM_HEADER + sizeof stream, &f));
    CHECK(!film_parse(data, 10, &f));
}

// BT.601 limited range forward transform, rounded
static void to_yuv(u32 rgb, u8 *y, u8 *u, u8 *v) {
    double r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    *y = (u8)(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255 + 0.5);
    *u = (u8)(128 + (-37.797 * r - 74.203 * g + 112.0 * b) / 255 + 0.5);
    *v = (u8)(128 + (112.0 * r - 93.786 * g - 18.214 * b) / 255 + 0.5);
}

static int close_rgb(u32 a, u32 b) {
    for (int s = 0; s < 24; s += 8) {
        int d = (int)((a >> s) & 255) - (int)((b >> s) & 255);
        if (d < -3 || d > 3) return 0;
    }
    return 1;
}

static void test_colour(void) {
    // A 4x2 picture: left half one colour, right half another
    const u32 colours[] = {0xf38ba8, 0x171717, 0xffffff, 0x000000, 0xa6e3a1, 0x89b4fa};
    for (int k = 0; k + 1 < 6; k++) {
        u8 y[8], u[2], v[2];
        for (int half = 0; half < 2; half++) {
            u8 yy, uu, vv;
            to_yuv(colours[k + half], &yy, &uu, &vv);
            for (int row = 0; row < 2; row++) y[row * 4 + half * 2] = y[row * 4 + half * 2 + 1] = yy;
            u[half] = uu, v[half] = vv;
        }
        u32 out[8];
        yuv420_to_rgb(y, u, v, 4, 4, 2, out, 4, 4, 2);
        CHECK(close_rgb(out[0], colours[k]) && close_rgb(out[5], colours[k]));
        CHECK(close_rgb(out[3], colours[k + 1]) && close_rgb(out[6], colours[k + 1]));
    }
}

static int grey(u32 rgb, int lo, int hi) {
    int r = (int)(rgb >> 16 & 255), g = (int)(rgb >> 8 & 255), b = (int)(rgb & 255);
    return r >= lo && r <= hi && g >= lo && g <= hi && b >= lo && b <= hi;
}

static void test_shrink(void) {
    // 4x2 to 2x1: every pixel averages the two columns it covers, the stripes turn grey
    u8 y[8] = {16, 235, 16, 235, 16, 235, 16, 235}, u[2] = {128, 128}, v[2] = {128, 128};
    u32 out[4];
    yuv420_to_rgb(y, u, v, 4, 4, 2, out, 2, 2, 1);
    CHECK(grey(out[0], 120, 136) && grey(out[1], 120, 136));
    // 3 columns to 2: the first pixel covers the white column alone, the second averages two
    yuv420_to_rgb(y + 1, u, v, 4, 3, 2, out, 2, 2, 1);
    CHECK(out[0] == 0xffffff && grey(out[1], 120, 136));
    // A one pixel line in 5 columns shrunk to 4 stays visible
    u8 line[10] = {16, 16, 235, 16, 16, 16, 16, 235, 16, 16};
    yuv420_to_rgb(line, u, v, 5, 5, 2, out, 4, 4, 1);
    u32 brightest = 0;
    for (int i = 0; i < 4; i++) brightest = (out[i] & 255) > brightest ? (out[i] & 255) : brightest;
    CHECK(brightest > 100);
}

static void test_same_size(void) {
    // Without scaling every pixel keeps its own luma
    u8 y[8] = {16, 235, 16, 235, 16, 235, 16, 235}, u[2] = {128, 128}, v[2] = {128, 128};
    u32 out[8];
    yuv420_to_rgb(y, u, v, 4, 4, 2, out, 4, 4, 2);
    CHECK(out[0] == 0x000000 && out[1] == 0xffffff && out[7] == 0xffffff);
}

int main(void) {
    test_parse_and_index();
    test_colour();
    test_shrink();
    test_same_size();
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
