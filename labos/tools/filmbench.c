// Decoding speed of a labos film on the host: h264bsd plus the colour conversion of labos (src/film.c).
// Usage: filmbench FILM.lfilm [first_frame.ppm]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "film.h"
#include "h264bsd_decoder.h"

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *data = malloc(size);
    if (fread(data, 1, size, f) != (size_t)size) return 1;
    fclose(f);
    u32 header[5];
    memcpy(header, data + 8, sizeof header);
    u8 *stream = data + 28;
    u32 left = (u32)(size - 28), frames = 0;
    storage_t *dec = h264bsdAlloc();
    h264bsdInit(dec, 1);
    double start = now(), convert = 0;
    while (left > 0) {
        u32 used = 0;
        u32 status = h264bsdDecode(dec, stream, left, 0, &used);
        stream += used;
        left -= used;
        if (status == H264BSD_PIC_RDY) {
            u32 id, idr, errors;
            double t0 = now();
            u8 *pic = h264bsdNextOutputPicture(dec, &id, &idr, &errors);
            u32 stride = h264bsdPicWidth(dec) * 16, rows = h264bsdPicHeight(dec) * 16;
            static u32 *bgra;
            if (!bgra) bgra = malloc((size_t)header[0] * header[1] * 4);
            const u8 *u = pic + (size_t)stride * rows, *v = u + (size_t)(stride / 2) * (rows / 2);
            yuv420_to_rgb(pic, u, v, stride, header[0], header[1], bgra, header[0], header[0], header[1]);
            convert += now() - t0;
            if (frames == 300 && argc > 2) {
                FILE *ppm = fopen(argv[2], "wb");
                u32 w = header[0], h = header[1];
                fprintf(ppm, "P6\n%u %u\n255\n", w, h);
                for (u32 i = 0; i < w * h; i++) {
                    u8 *p = (u8 *)&bgra[i];
                    fputc(p[2], ppm), fputc(p[1], ppm), fputc(p[0], ppm);
                }
                fclose(ppm);
            }
            frames++;
        }
        if (status == H264BSD_ERROR || status == H264BSD_PARAM_SET_ERROR) {
            printf("decode error after %u frames\n", frames);
            return 1;
        }
    }
    double total = now() - start;
    printf("%ux%u: %u frames (header says %u) in %.2f s: %.1f fps, conversion %.1f%%\n", header[0], header[1],
           frames, header[3], total, frames / total, 100 * convert / total);
    return 0;
}
