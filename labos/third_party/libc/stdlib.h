// Just enough of the C library for third party code in labos; lib.c implements it on UEFI boot services.
#ifndef LABOS_LIBC_STDLIB_H
#define LABOS_LIBC_STDLIB_H
#include <stddef.h>
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void free(void *ptr);
#endif
