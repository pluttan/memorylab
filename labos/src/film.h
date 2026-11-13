// Film files of labos (tools/mkfilm.py) and the conversion of decoded pictures. Pure: tests run it on the host.
#ifndef LABOS_FILM_H
#define LABOS_FILM_H

#include "efi.h"

#define FILM_HEADER 28

typedef struct {
    u32 width, height, fps, frames, gop;
    const u8 *stream;  // H.264 Annex B
    usize length;
} film_info;

// 1 when the data starts with a valid header
int film_parse(const u8 *data, usize size, film_info *out);

// Offsets in the stream of every SPS, each one starts a group of pictures; returns how many (at most cap)
usize film_index(const u8 *stream, usize length, usize *offsets, usize cap);

// End of the NAL unit that starts at pos (with its start code): where the next start code begins, or length
usize film_nal_end(const u8 *stream, usize length, usize pos);

// A decoded YUV 4:2:0 picture (BT.601, limited range) to 0x00RRGGBB pixels, scaled to dst_w x dst_h by the
// nearest pixel. stride: bytes per luma row of the source; dst_pitch: pixels per row of the destination
void yuv420_to_rgb(const u8 *y, const u8 *u, const u8 *v, u32 stride, u32 src_w, u32 src_h, u32 *dst, u32 dst_pitch,
                   u32 dst_w, u32 dst_h);

#endif
