#include "lib.h"

#include <stdarg.h>

// The Windows ABI that UEFI uses marks floating point code with a reference to this symbol
int _fltused = 0;

efi_system_table *ST;
efi_boot_services *BS;
efi_handle IMAGE;

// ==========================
// ===  Memory            ===
// ==========================

// The compiler emits calls to these for struct copies and large zeroing
void *memset(void *dst, int value, usize size) {
    u8 *d = dst;
    while (size--) *d++ = (u8)value;
    return dst;
}

void *memcpy(void *dst, const void *src, usize size) {
    u8 *d = dst;
    const u8 *s = src;
    while (size--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, usize size) {
    u8 *d = dst;
    const u8 *s = src;
    if (d < s) {
        while (size--) *d++ = *s++;
    } else {
        while (size--) d[size] = s[size];
    }
    return dst;
}

int memcmp(const void *a, const void *b, usize size) {
    const u8 *x = a, *y = b;
    for (usize i = 0; i < size; i++) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

usize strlen(const char *s) {
    usize n = 0;
    while (s[n]) n++;
    return n;
}

int strncmp(const char *a, const char *b, usize n) {
    for (usize i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (u8)a[i] - (u8)b[i];
    }
    return 0;
}

void *alloc(usize size) {
    void *ptr = 0;
    if (BS->allocate_pool(EFI_LOADER_DATA, size, &ptr) != EFI_SUCCESS) return 0;
    return ptr;
}

void release(void *ptr) {
    if (ptr) BS->free_pool(ptr);
}

// For third party code (h264bsd): third_party/libc/stdlib.h declares these
void *malloc(usize size) { return alloc(size); }

void *calloc(usize count, usize size) {
    void *ptr = alloc(count * size);
    if (ptr) memset(ptr, 0, count * size);
    return ptr;
}

void free(void *ptr) { release(ptr); }

void *alloc_pages(usize pages) {
    efi_phys_addr addr = 0;
    if (BS->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA, pages, &addr) != EFI_SUCCESS) return 0;
    return (void *)addr;
}

u64 memory_total_bytes(void) {
    usize size = 0, key = 0, desc_size = 0;
    u32 version = 0;
    BS->get_memory_map(&size, 0, &key, &desc_size, &version);
    size += 8 * desc_size;  // the allocation below may add descriptors
    u8 *map = alloc(size);
    if (!map || BS->get_memory_map(&size, (efi_memory_descriptor *)map, &key, &desc_size, &version) != EFI_SUCCESS) {
        release(map);
        return 0;
    }
    u64 pages = 0;
    for (usize off = 0; off + desc_size <= size; off += desc_size) {
        efi_memory_descriptor *d = (efi_memory_descriptor *)(map + off);
        // Loader, boot and runtime services, conventional and ACPI memory are RAM; MMIO and reserved are not
        if ((d->type >= 1 && d->type <= 7) || d->type == 9 || d->type == 10) pages += d->number_of_pages;
    }
    release(map);
    // Firmware holes make the sum a little smaller than the installed modules
    u64 gb = 1ull << 30;
    return (pages * 4096 + gb - 1) / gb * gb;
}

// ==========================
// ===  Time              ===
// ==========================

double tsc_per_ns = 1;

void time_init(void) {
    u64 t0 = rdtsc();
    BS->stall(50000);
    u64 t1 = rdtsc();
    tsc_per_ns = (double)(t1 - t0) / 50e6;
    if (tsc_per_ns <= 0) tsc_per_ns = 1;
}

u64 now_us(void) { return (u64)((double)rdtsc() / tsc_per_ns / 1000); }

// ==========================
// ===  Log               ===
// ==========================

// The UEFI console would print over the graphs, so the log goes to COM1 (QEMU writes it to a file)
// and to a ring that the network command "log" returns
static inline void outb(u16 port, u8 value) { __asm__ volatile("outb %0, %1" ::"a"(value), "Nd"(port)); }
static inline u8 inb(u16 port) {
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

#define COM1 0x3f8
static int serial_ready;
char log_ring[LOG_RING_SIZE];
usize log_head;

static void serial_init(void) {
    outb(COM1 + 1, 0x00);  // no interrupts
    outb(COM1 + 3, 0x80);  // divisor latch
    outb(COM1 + 0, 0x01);  // 115200 baud
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);  // 8N1
    outb(COM1 + 2, 0xc7);  // FIFO on
    serial_ready = inb(COM1 + 5) != 0xff ? 1 : -1;  // 0xff means there is no UART
}

static void serial_put(char c) {
    for (int i = 0; i < 100000 && !(inb(COM1 + 5) & 0x20); i++) {
    }
    outb(COM1, (u8)c);
}

// The error messages of h264bsd (third_party/libc/stdio.h)
void labos_log_printf(const char *format, ...) {
    char text[256];
    va_list ap;
    va_start(ap, format);
    usize n = vfmt(text, sizeof text, format, ap);
    va_end(ap);
    if (n && n < sizeof text && text[n - 1] == '\n') text[n - 1] = 0;
    log_line("h264bsd: %s", text);
}

void log_line(const char *format, ...) {
    char text[512];
    va_list ap;
    va_start(ap, format);
    usize n = vfmt(text, sizeof text - 1, format, ap);
    va_end(ap);
    if (n > sizeof text - 2) n = sizeof text - 2;
    text[n++] = '\n';
    if (!serial_ready) serial_init();
    for (usize i = 0; i < n; i++) {
        if (serial_ready > 0) {
            if (text[i] == '\n') serial_put('\r');
            serial_put(text[i]);
        }
        log_ring[log_head++ % LOG_RING_SIZE] = text[i];
    }
}
