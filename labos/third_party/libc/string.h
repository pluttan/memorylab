#ifndef LABOS_LIBC_STRING_H
#define LABOS_LIBC_STRING_H
#include <stddef.h>
void *memset(void *dst, int value, size_t size);
void *memcpy(void *dst, const void *src, size_t size);
void *memmove(void *dst, const void *src, size_t size);
int memcmp(const void *a, const void *b, size_t size);
#endif
