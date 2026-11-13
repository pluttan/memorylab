#include "player.h"

#include "film.h"
#include "fs.h"
#include "gfx.h"
#include "h264bsd_decoder.h"
#include "lib.h"

#define OVERLAY_US 3000000
#define SEEK_FRAMES_MAX 5  // at most this many late frames in a row go undrawn

static const u32 HEIGHTS[] = {1080, 720};

static struct {
    u8 *data;
    usize size;
    film_info film;
    usize *gops;
    usize ngops;
    storage_t *dec;
    usize pos;      // start of the next NAL unit in the stream
    // The decoder strips emulation bytes in place, so it gets a copy of each NAL unit and the stream stays
    // intact for seeking back. When a picture is ready it reads 0 bytes and wants the same pointer again
    u8 *nal;
    usize nal_cap, nal_len, nal_used;
    u32 frame;      // index of the next frame to show
    u64 start_us;   // when frame 0 was (or would have been) shown
    int paused, ended;
    u64 overlay_until;
    int overlay_shown;
    i32 x, y, w, h;  // where the picture goes on the screen
    u32 drawn, dropped, late_run;
    u64 first_us;
    char error[160];
} P;

const char *player_error(void) { return P.error; }

static u64 frame_us(u32 frame) { return (u64)frame * 1000000ull / P.film.fps; }

// ==========================
// ===  Opening           ===
// ==========================

static int load(const char *name, u32 height) {
    char path[96];
    fmt(path, sizeof path, "labos/films/%s-%u.lfilm", name, height);
    P.data = fs_read(path, &P.size);
    if (!P.data) return 0;
    if (!film_parse(P.data, P.size, &P.film)) {
        log_line("player: %s is not a labos film", path);
        fs_release(P.data, P.size);
        P.data = 0;
        return 0;
    }
    // The sum lets the log be checked against the file on the stick
    u32 sum = 0;
    for (usize i = 0; i < P.size; i++) sum = sum * 31 + P.data[i];
    log_line("player: %s, %ux%u, %u frames, %lu bytes, sum %x", path, P.film.width, P.film.height, P.film.frames,
             P.size, sum);
    return 1;
}

static void place_picture(void) {
    u32 w = P.film.width, h = P.film.height;
    if (w > screen.width || h > screen.height) {
        // Shrink to fit, keeping the proportions
        u64 sw = (u64)screen.width * h, sh = (u64)screen.height * w;
        if (sw < sh) {
            h = (u32)((u64)h * screen.width / w), w = screen.width;
        } else {
            w = (u32)((u64)w * screen.height / h), h = screen.height;
        }
    }
    P.w = (i32)w, P.h = (i32)h;
    P.x = ((i32)screen.width - P.w) / 2, P.y = ((i32)screen.height - P.h) / 2;
}

int player_open(const char *name) {
    player_close();
    P.error[0] = 0;
    // The largest film that fits the screen; the smallest one is shrunk when nothing fits
    int found = 0;
    for (usize i = 0; i < sizeof HEIGHTS / sizeof HEIGHTS[0] && !found; i++) {
        u32 width = HEIGHTS[i] * 16 / 9;
        if ((width <= screen.width && HEIGHTS[i] <= screen.height) || i + 1 == sizeof HEIGHTS / sizeof HEIGHTS[0]) {
            found = load(name, HEIGHTS[i]);
        }
    }
    for (usize i = 0; i < sizeof HEIGHTS / sizeof HEIGHTS[0] && !found; i++) found = load(name, HEIGHTS[i]);
    if (!found) {
        fmt(P.error, sizeof P.error, "Ролика нет на флешке: labos/films/%s-720.lfilm. Запишите его: make usb.", name);
        return 0;
    }
    usize cap = P.film.frames / P.film.gop + 2;
    P.gops = alloc(cap * sizeof *P.gops);
    P.dec = h264bsdAlloc();
    if (!P.gops || !P.dec || h264bsdInit(P.dec, 1) != H264BSD_RDY) {
        fmt(P.error, sizeof P.error, "Ролику не хватило памяти.");
        player_close();
        return 0;
    }
    P.ngops = film_index(P.film.stream, P.film.length, P.gops, cap);
    place_picture();
    gfx_fill(0, 0, screen.width, screen.height, COL_OUTER);
    gfx_present();
    P.frame = 0;
    P.pos = P.ngops ? P.gops[0] : 0;
    P.start_us = P.first_us = now_us();
    P.overlay_until = P.start_us + OVERLAY_US;
    return 1;
}

void player_close(void) {
    if (P.dec) {
        h264bsdShutdown(P.dec);
        h264bsdFree(P.dec);
    }
    release(P.gops);
    release(P.nal);
    fs_release(P.data, P.size);
    char error[sizeof P.error];
    memcpy(error, P.error, sizeof error);
    memset(&P, 0, sizeof P);
    memcpy(P.error, error, sizeof error);
}

// ==========================
// ===  Overlay           ===
// ==========================

static i32 overlay_height(void) { return gfx_line_height() * 2 + 28; }

static void time_text(char *out, usize size, u32 frame) {
    u32 s = frame / P.film.fps, total = P.film.frames / P.film.fps;
    fmt(out, size, "%u:%02u / %u:%02u", s / 60, s % 60, total / 60, total % 60);
}

static void draw_overlay(void) {
    i32 h = overlay_height(), y = (i32)screen.height - h;
    gfx_fill(0, y, screen.width, h, COL_GRID);
    i32 bar_x = 24, bar_w = (i32)screen.width - 48, bar_y = y + 14;
    gfx_fill(bar_x, bar_y, bar_w, 6, COL_AXIS);
    u32 shown = P.frame ? P.frame - 1 : 0;
    gfx_fill(bar_x, bar_y, (i32)((u64)bar_w * shown / (P.film.frames ? P.film.frames : 1)), 6, COL_MAUVE);
    char t[48];
    time_text(t, sizeof t, shown);
    i32 ty = bar_y + 14;
    const char *state = P.ended ? "Конец ролика" : P.paused ? "Пауза" : "";
    gfx_text(bar_x, ty, t, COL_TEXT);
    if (*state) gfx_text(bar_x + gfx_text_width(t) + 24, ty, state, COL_PEACH);
    const char *keys = "Пробел: пауза    Влево/вправо: 10 с    Home: сначала    Esc: назад";
    gfx_text(bar_x + bar_w - gfx_text_width(keys), ty, keys, COL_SUBTEXT);
}

static void present_overlay(int visible) {
    i32 h = overlay_height(), y = (i32)screen.height - h;
    if (visible) {
        draw_overlay();
    } else {
        gfx_fill(0, y, screen.width, h, COL_OUTER);
    }
    gfx_present_rect(0, y, (i32)screen.width, h);
    P.overlay_shown = visible;
}

// ==========================
// ===  Decoding          ===
// ==========================

// Feeds the decoder until it gives a picture; 0 at the end of the stream
// Copies the next NAL unit into the decoder buffer; 0 at the end of the stream or without memory
static int take_nal(void) {
    if (P.pos >= P.film.length) return 0;
    usize end = film_nal_end(P.film.stream, P.film.length, P.pos), length = end - P.pos;
    if (length > P.nal_cap) {
        release(P.nal);
        P.nal_cap = length * 2;
        P.nal = alloc(P.nal_cap);
        if (!P.nal) {
            P.nal_cap = 0;
            return 0;
        }
    }
    memcpy(P.nal, P.film.stream + P.pos, length);
    P.pos = end;
    P.nal_len = length;
    P.nal_used = 0;
    return 1;
}

// A ready picture or new headers come back with 0 bytes read: the decoder wants the same unit again.
// More empty reads in a row than that mean a unit it will never take
#define EMPTY_READS_MAX 4

static u8 *next_picture(void) {
    int empty = 0;
    for (;;) {
        if (P.nal_used >= P.nal_len && !take_nal()) return 0;
        u32 used = 0;
        u32 status = h264bsdDecode(P.dec, P.nal + P.nal_used, (u32)(P.nal_len - P.nal_used), 0, &used);
        P.nal_used += used;
        empty = used ? 0 : empty + 1;
        if (status == H264BSD_PIC_RDY) {
            u32 id, idr, errors;
            return h264bsdNextOutputPicture(P.dec, &id, &idr, &errors);
        }
        if (status == H264BSD_ERROR || status == H264BSD_PARAM_SET_ERROR) {
            log_line("player: decode status %u before byte %lu", status, P.pos);
            P.nal_used = P.nal_len;  // skip the broken unit
        } else if (empty > EMPTY_READS_MAX) {
            P.nal_used = P.nal_len;
        }
    }
}

static void draw_picture(const u8 *pic) {
    u32 stride = h264bsdPicWidth(P.dec) * 16, rows = h264bsdPicHeight(P.dec) * 16;
    const u8 *y = pic, *u = pic + (usize)stride * rows, *v = u + (usize)(stride / 2) * (rows / 2);
    u32 *dst = screen.pixels + (usize)P.y * screen.width + P.x;
    yuv420_to_rgb(y, u, v, stride, P.film.width, P.film.height, dst, screen.width, (u32)P.w, (u32)P.h);
}

void player_tick(void) {
    if (!P.dec) return;
    u64 now = now_us();
    int want_overlay = P.paused || P.ended || now < P.overlay_until;
    if (P.paused || P.ended) {
        if (want_overlay != P.overlay_shown) present_overlay(want_overlay);
        return;
    }
    u64 due = P.start_us + frame_us(P.frame);
    if (now < due) {
        if (want_overlay != P.overlay_shown) present_overlay(want_overlay);
        return;
    }
    u8 *pic = next_picture();
    if (!pic || P.frame >= P.film.frames) {
        P.ended = 1;
        present_overlay(1);
        log_line("player: end, %u drawn, %u dropped", P.drawn, P.dropped);
        return;
    }
    P.frame++;
    // A frame later than one period goes undrawn, so that a slow machine keeps the pace of the film
    int late = now > due + frame_us(1);
    if (late && P.late_run < SEEK_FRAMES_MAX) {
        P.late_run++;
        P.dropped++;
        return;
    }
    P.late_run = 0;
    P.drawn++;
    draw_picture(pic);
    if (want_overlay) draw_overlay();
    gfx_present_rect(P.x, P.y, P.w, P.h);
    if (want_overlay || P.overlay_shown) present_overlay(want_overlay);
}

// ==========================
// ===  Control           ===
// ==========================

void player_toggle_pause(void) {
    if (!P.dec || P.ended) return;
    P.paused = !P.paused;
    if (!P.paused) P.start_us = now_us() - frame_us(P.frame);
    P.overlay_until = now_us() + OVERLAY_US;
    present_overlay(1);
}

static void start_at_gop(usize g) {
    if (g >= P.ngops) g = P.ngops ? P.ngops - 1 : 0;
    h264bsdShutdown(P.dec);
    h264bsdInit(P.dec, 1);
    P.pos = P.ngops ? P.gops[g] : 0;
    P.nal_len = P.nal_used = 0;
    P.frame = (u32)(g * P.film.gop);
    P.start_us = now_us() - frame_us(P.frame);
    P.ended = 0;
    P.late_run = 0;
    P.overlay_until = now_us() + OVERLAY_US;
}

void player_seek(int seconds) {
    if (!P.dec) return;
    i64 target = (i64)P.frame + (i64)seconds * P.film.fps;
    if (target < 0) target = 0;
    if (target >= P.film.frames) target = P.film.frames - 1;
    start_at_gop((usize)target / P.film.gop);
    if (P.paused) {
        // Show the frame the seek landed on
        u8 *pic = next_picture();
        if (pic) {
            P.frame++;
            draw_picture(pic);
            gfx_present_rect(P.x, P.y, P.w, P.h);
        }
    }
    present_overlay(1);
}

void player_restart(void) {
    if (!P.dec) return;
    P.paused = 0;
    player_seek(-(int)(P.frame / P.film.fps) - 1);
}

usize player_state(char *out, usize cap) {
    if (!P.dec) return fmt(out, cap, "no film");
    double seconds = (double)(now_us() - P.first_us) / 1e6;
    return fmt(out, cap, "film %ux%u at %d,%d size %dx%d, frame %u of %u, drawn %u, dropped %u, %.1f s%s%s",
               P.film.width, P.film.height, P.x, P.y, P.w, P.h, P.frame, P.film.frames, P.drawn, P.dropped, seconds,
               P.paused ? ", paused" : "", P.ended ? ", ended" : "");
}
