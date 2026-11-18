#include "results.h"

#include "bmp.h"
#include "fs.h"
#include "gfx.h"
#include "lib.h"

#define SCRATCH_PAGES 4096  // 16 MB: a 1920x1080 screenshot takes 6 MB, the largest report a few MB

static u8 *scratch;

static usize log_text(u8 *out, usize cap) {
    usize kept = log_head < LOG_RING_SIZE ? log_head : LOG_RING_SIZE;
    if (kept > cap) kept = cap;
    for (usize i = 0; i < kept; i++) out[i] = (u8)log_ring[(log_head - kept + i) % LOG_RING_SIZE];
    return kept;
}

int results_save(const char *id, usize (*report)(u8 *out, usize cap), char *saved, usize size) {
    if (!scratch) scratch = alloc_pages(SCRATCH_PAGES);
    if (!scratch) return 0;
    usize cap = (usize)SCRATCH_PAGES * 4096;
    efi_time t = {0};
    // Without a clock the name still differs between runs: the TSC counts since power on
    if (ST->runtime_services->get_time(&t, 0) != EFI_SUCCESS) t.year = 0;
    char base[96], path[128];
    if (t.year) {
        fmt(base, sizeof base, "labos/results/%s-%04u%02u%02u-%02u%02u%02u", id, t.year, t.month, t.day, t.hour,
            t.minute, t.second);
    } else {
        fmt(base, sizeof base, "labos/results/%s-%lu", id, now_us() / 1000);
    }
    int ok = 1;
    fmt(path, sizeof path, "%s.txt", base);
    ok &= fs_write(path, scratch, report(scratch, cap));
    fmt(saved, size, "%s", path);
    fmt(path, sizeof path, "%s.bmp", base);
    usize n = bmp_encode(screen.pixels, screen.width, screen.height, scratch, cap);
    ok &= n && fs_write(path, scratch, n);
    ok &= fs_write("labos/log.txt", scratch, log_text(scratch, cap));
    log_line("results: %s %s", base, ok ? "saved" : "not saved");
    return ok;
}
