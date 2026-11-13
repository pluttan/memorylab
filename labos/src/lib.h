// UEFI globals, memory helpers and the log.
#ifndef LABOS_LIB_H
#define LABOS_LIB_H

#include "efi.h"
#include "fmt.h"

extern efi_system_table *ST;
extern efi_boot_services *BS;
extern efi_handle IMAGE;

void *memset(void *dst, int value, usize size);
void *memcpy(void *dst, const void *src, usize size);
void *memmove(void *dst, const void *src, usize size);
int memcmp(const void *a, const void *b, usize size);
usize strlen(const char *s);
int strncmp(const char *a, const char *b, usize n);

void *alloc(usize size);
void release(void *ptr);
// Physically contiguous pages; UEFI maps memory one to one, so the address is both virtual and physical
void *alloc_pages(usize pages);
// RAM in the UEFI memory map (usable, firmware and ACPI), rounded up to a whole GB
u64 memory_total_bytes(void);

// TSC clock, calibrated against the firmware Stall() at start
static inline u64 rdtsc(void) {
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (u64)hi << 32 | lo;
}
void time_init(void);
u64 now_us(void);
extern double tsc_per_ns;

// One line to COM1 and to the log ring (the console is not used: it would print over the graphs)
void log_line(const char *format, ...);
#define LOG_RING_SIZE 16384
extern char log_ring[LOG_RING_SIZE];
extern usize log_head;  // bytes written since boot; the ring keeps the last LOG_RING_SIZE

#endif
