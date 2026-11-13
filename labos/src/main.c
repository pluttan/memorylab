// labos: memory experiments on bare hardware, graphs on the screen, control over the network.
#include "exp_rowconf.h"
#include "gfx.h"
#include "lib.h"
#include "net.h"
#include "plot.h"
#include "remote.h"

#define SCAN_ESC 0x17
#define EFI_RESET_COLD 0

static void draw_status(const char *text) {
    i32 h = gfx_line_height() + 8;
    gfx_fill(0, screen.height - h, screen.width, h, COL_GRID);
    gfx_text(12, screen.height - h + 4, text, COL_TEXT);
}

// A self check of the drawing code until the experiments arrive
static void draw_demo(void) {
    static double xs[64], quad[64], lin[64], sizes[24], times[24];
    for (int i = 0; i < 64; i++) {
        xs[i] = i;
        quad[i] = i * i / 40.0;
        lin[i] = i * 1.5;
    }
    for (int i = 0; i < 24; i++) {
        sizes[i] = (double)(1024ull << (i / 2)) * (i % 2 ? 1.5 : 1);
        times[i] = sizes[i] < 32768 ? 3 : sizes[i] < 262144 ? 8 : sizes[i] < 6e6 ? 20 : 60;
    }
    gfx_fill(0, 0, screen.width, screen.height, COL_OUTER);
    i32 half = (screen.height - gfx_line_height() - 8) / 2;
    plot a = {.x = 0, .y = 0, .w = screen.width, .h = half, .x0 = 0, .x1 = 63, .y0 = 0, .y1 = 100,
              .title = "Проверка рисования: линии", .x_label = "Номер точки", .y_label = "нс"};
    plot_frame(&a);
    plot_line(&a, xs, quad, 64, COL_RED);
    plot_line(&a, xs, lin, 64, COL_GREEN);
    legend_item items[] = {{"x² / 40", COL_RED}, {"1,5 x", COL_GREEN}};
    plot_legend(&a, items, 2);
    plot b = {.x = 0, .y = half, .w = screen.width, .h = half, .x0 = 1024, .x1 = sizes[23], .y0 = 0, .y1 = 70,
              .log_x = 1, .x_kind = AXIS_BYTES, .title = "Проверка рисования: логарифмическая ось",
              .x_label = "Размер блока", .y_label = "нс/КБ"};
    plot_frame(&b);
    plot_line(&b, sizes, times, 24, COL_MAUVE);
    plot_dots(&b, sizes, times, 24, COL_BLUE);
    gfx_present();
}

static char shown_status[128];

// The bottom line shows the address labctl needs; redrawn when it changes
static void update_status(void) {
    char *shown = shown_status;
    char ip[32], text[128];
    net_status(ip, sizeof ip);
    fmt(text, sizeof text, "labos · %s · 1: опыт 1 · Esc: перезагрузка", ip);
    if (!strncmp(text, shown, sizeof shown_status)) return;
    memcpy(shown, text, sizeof shown_status);
    draw_status(text);
    gfx_present();
}

// ==========================
// ===  Experiments       ===
// ==========================

static rowconf_result *rowconf;

static int run_rowconf(int rounds) {
    if (!rowconf) rowconf = alloc_pages((sizeof *rowconf + 4095) / 4096);
    if (!rowconf || !rowconf_run(rowconf, rounds)) return 0;
    rowconf_draw(rowconf);
    shown_status[0] = 0;  // the graph covered the status line
    update_status();
    return 1;
}

static int parse_int(const char *s, int fallback) {
    int v = 0, any = 0;
    while (*s == ' ') s++;
    for (; *s >= '0' && *s <= '9'; s++, any = 1) v = v * 10 + (*s - '0');
    return any ? v : fallback;
}

// "run rowconf [rounds]"
static usize cmd_run(const char *args, u8 *out, usize cap) {
    if (!strncmp(args, "rowconf", 7)) {
        if (!run_rowconf(parse_int(args + 7, 15))) return fmt((char *)out, cap, "error: no memory");
        return rowconf_report(rowconf, out, cap);
    }
    return fmt((char *)out, cap, "error: unknown experiment '%s'", args);
}

efi_status efi_main(efi_handle image, efi_system_table *st) {
    ST = st;
    BS = st->boot_services;
    IMAGE = image;
    // UEFI resets the machine after five minutes of a running application unless the watchdog is off
    BS->set_watchdog_timer(0, 0, 0, 0);
    time_init();
    log_line("labos: start, firmware revision %x, TSC %.3f GHz", st->firmware_revision, tsc_per_ns);
    if (!gfx_init()) return EFI_ERR(1);
    draw_demo();
    if (net_init()) {
        if (remote_init()) {
            remote_register("run", cmd_run);
        } else {
            log_line("labos: no memory for the remote buffer");
        }
    }
    log_line("labos: ready");
    u64 reset_at = 0;
    for (;;) {
        net_poll();
        update_status();
        efi_input_key key;
        if (ST->con_in->read_key_stroke(ST->con_in, &key) == EFI_SUCCESS) {
            if (key.scan_code == SCAN_ESC) remote_reset_requested = 1;
            if (key.unicode_char == '1') run_rowconf(15);
        }
        if (remote_reset_requested && !reset_at) reset_at = now_us() + 200000;
        if (reset_at && now_us() > reset_at) {
            log_line("labos: reset");
            ST->runtime_services->reset_system(EFI_RESET_COLD, EFI_SUCCESS, 0, 0);
        }
        BS->stall(100);
    }
}
