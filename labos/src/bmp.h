// 24-bit BMP of the screen for the results on the stick. Pure: tests run it on the host.
#ifndef LABOS_BMP_H
#define LABOS_BMP_H

#include "efi.h"

usize bmp_size(u32 width, u32 height);

// pixels are 0x00RRGGBB rows from the top; returns the size written, 0 when cap is too small
usize bmp_encode(const u32 *pixels, u32 width, u32 height, u8 *out, usize cap);

#endif
