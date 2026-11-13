// printf subset and UTF-8 decoding. No UEFI calls: tests run it on the host.
#ifndef LABOS_FMT_H
#define LABOS_FMT_H

#include <stdarg.h>

#include "efi.h"

// %d %u %x %s %c %f (%.Nf), %ld %lu %lx, %%; width pads with spaces or with zeros after 0
usize fmt(char *out, usize size, const char *format, ...);
usize vfmt(char *out, usize size, const char *format, va_list ap);

// Decodes one UTF-8 sequence starting at *s and moves *s past it
u32 utf8_next(const char **s);

#endif
