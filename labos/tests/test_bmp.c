// Host test of the BMP writer: header fields, row order, padding.
#include <stdio.h>
#include <string.h>

#include "bmp.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

static unsigned get32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

int main(void) {
    // 3x2: top row red, green, blue; bottom row white, black, peach. Rows of 9 bytes pad to 12
    const u32 px[6] = {0xff0000, 0x00ff00, 0x0000ff, 0xffffff, 0x000000, 0xfab387};
    unsigned char out[128];
    CHECK(bmp_size(3, 2) == 54 + 24);
    usize n = bmp_encode(px, 3, 2, out, sizeof out);
    CHECK(n == 78);
    CHECK(out[0] == 'B' && out[1] == 'M' && get32(out + 2) == 78 && get32(out + 10) == 54);
    CHECK(get32(out + 18) == 3 && get32(out + 22) == 2 && out[28] == 24);
    // The first row in the file is the bottom one, bytes B, G, R
    CHECK(out[54] == 0xff && out[55] == 0xff && out[56] == 0xff);
    CHECK(out[54 + 6] == 0x87 && out[54 + 7] == 0xb3 && out[54 + 8] == 0xfa);
    CHECK(out[54 + 9] == 0 && out[54 + 11] == 0);  // padding
    CHECK(out[66] == 0 && out[67] == 0 && out[68] == 0xff);  // red, top row
    CHECK(bmp_encode(px, 3, 2, out, 70) == 0);
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
