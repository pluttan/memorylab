#include "manual.h"

#include "fs.h"
#include "gfx.h"
#include "lib.h"
#include "manualfmt.h"
#include "widgets.h"

#define MAX_BLOCKS 2048
#define MAX_SECTIONS 16
#define MAX_TABLE_CELLS 64
#define PAGE_MAX_WIDTH 1180
#define INSET 44

typedef struct {
    const char *id;
    const char *film;
    i32 y;
} section;

static struct {
    char *text;
    usize size;
    mblock *blocks;
    usize nblocks;
    section sections[MAX_SECTIONS];
    int nsections;
    surface page;
    usize page_pages;
    i32 top, view_h;
    int ready;
    char error[160];
} M;

const char *manual_error(void) { return M.error; }

// ==========================
// ===  Layout            ===
// ==========================

static i32 code_run(usize i, i32 x, i32 y, i32 width) {
    i32 lh = gfx_line_height() + 2;
    usize end = i;
    while (end < M.nblocks && M.blocks[end].kind == MB_CODE) end++;
    i32 h = (i32)(end - i) * lh + 24;
    gfx_fill(x - 16, y, width + 32, h, COL_BOX);
    for (usize k = i; k < end; k++) gfx_text(x, y + 12 + (i32)(k - i) * lh, M.blocks[k].text, COL_TEXT);
    return h;
}

static i32 table_run(usize i, i32 x, i32 y, i32 width) {
    static char copies[MAX_TABLE_CELLS / 2][256];
    const char *cells[MAX_TABLE_CELLS];
    int ncols = 0, rows = 0;
    for (usize k = i; k < M.nblocks && rows < (int)(sizeof copies / sizeof copies[0]); k++) {
        if (M.blocks[k].kind != (k == i ? MB_TABLE_HEAD : MB_TABLE_ROW)) break;
        fmt(copies[rows], sizeof copies[rows], "%s", M.blocks[k].text);
        const char *row[8];
        int n = manual_cells(copies[rows], row, 8);
        if (!ncols) ncols = n;
        if ((rows + 1) * ncols > MAX_TABLE_CELLS) break;
        for (int c = 0; c < ncols; c++) cells[rows * ncols + c] = c < n ? row[c] : "";
        rows++;
    }
    return draw_table_wrap(x, y, width, ncols, cells, rows);
}

static i32 film_box(i32 x, i32 y) {
    const char *text = "Ролик к этому опыту: F2";
    i32 lh = gfx_line_height(), w = gfx_text_width(text) + 40;
    gfx_fill(x, y, w, lh + 16, COL_GREEN);
    gfx_fill(x + 2, y + 2, w - 4, lh + 12, COL_BOX);
    gfx_text(x + 20, y + 8, text, COL_GREEN);
    return lh + 16;
}

// Draws every block into the current screen surface (or only measures, under gfx_dry); returns the height
static i32 layout(i32 page_w) {
    i32 lh = gfx_line_height(), x = INSET, width = page_w - 2 * INSET, y = 36;
    M.nsections = 0;
    for (usize i = 0; i < M.nblocks; i++) {
        const mblock *b = &M.blocks[i];
        switch (b->kind) {
            case MB_SECTION:
                if (M.nsections < MAX_SECTIONS) M.sections[M.nsections++] = (section){b->text, 0, y > 36 ? y - 12 : 0};
                break;
            case MB_H1: y += wrap_text(x + (width - gfx_text_width(b->text)) / 2, y, width, b->text, COL_TEXT) + 6; break;
            case MB_H2: y += 18, y += wrap_text(x, y, width, b->text, COL_MAUVE) + 10; break;
            case MB_H3: y += 12, y += wrap_text(x, y, width, b->text, COL_PEACH) + 6; break;
            case MB_PARA: y += wrap_rich(x, y, width, b->text, COL_TEXT, COL_PEACH) + 10; break;
            case MB_ITEM: {
                const char *marker_end = b->text;
                while (*marker_end && *marker_end != ' ') marker_end++;
                gfx_text_n(x + 12, y, b->text, (usize)(marker_end - b->text), COL_MAUVE);
                const char *rest = *marker_end ? marker_end + 1 : marker_end;
                y += wrap_rich(x + 48, y, width - 48, rest, COL_TEXT, COL_PEACH) + 4;
                if (i + 1 < M.nblocks && M.blocks[i + 1].kind != MB_ITEM) y += 8;
                break;
            }
            case MB_QUOTE: {
                i32 h = wrap_rich(x + 20, y, width - 20, b->text, COL_SUBTEXT, COL_PEACH);
                gfx_fill(x, y, 3, h, COL_MAUVE);
                y += h + 10;
                break;
            }
            case MB_FORMULA:
                gfx_text(x + (width - gfx_text_width(b->text)) / 2, y + 4, b->text, COL_TEXT);
                y += lh + 16;
                break;
            case MB_CODE: {
                y += code_run(i, x, y, width) + 14;
                while (i + 1 < M.nblocks && M.blocks[i + 1].kind == MB_CODE) i++;
                break;
            }
            case MB_TABLE_HEAD:
            case MB_TABLE_ROW: {
                y += table_run(i, x, y, width) + 14;
                while (i + 1 < M.nblocks && M.blocks[i + 1].kind == MB_TABLE_ROW) i++;
                break;
            }
            case MB_FILM:
                if (M.nsections) M.sections[M.nsections - 1].film = b->text;
                y += 6, y += film_box(x, y) + 16;
                break;
            case MB_GAP:
                y += 24;
                gfx_line(x, y, x + width, y, COL_GRID, 1);
                y += 28;
                break;
        }
    }
    return y + 48;
}

static int load(void) {
    M.text = fs_read("labos/manual.txt", &M.size);
    if (!M.text) {
        fmt(M.error, sizeof M.error, "Методички нет на флешке: labos/manual.txt. Запишите её: make usb.");
        return 0;
    }
    M.blocks = alloc(MAX_BLOCKS * sizeof *M.blocks);
    if (!M.blocks) return 0;
    M.nblocks = manual_parse(M.text, M.size, M.blocks, MAX_BLOCKS);
    i32 page_w = (i32)screen.width - 48 < PAGE_MAX_WIDTH ? (i32)screen.width - 48 : PAGE_MAX_WIDTH;
    // The first pass only measures, the second draws into a page of the measured height
    surface saved = screen;
    screen.width = (u32)page_w, screen.height = 0x7fffffff;
    gfx_dry = 1;
    i32 height = layout(page_w);
    gfx_dry = 0;
    M.page_pages = ((usize)page_w * (usize)height * 4 + 4095) / 4096;
    M.page.pixels = alloc_pages(M.page_pages);
    if (!M.page.pixels) {
        screen = saved;
        fmt(M.error, sizeof M.error, "Методичке не хватило памяти.");
        return 0;
    }
    M.page.width = (u32)page_w, M.page.height = (u32)height;
    screen = M.page;
    gfx_fill(0, 0, page_w, height, COL_FIELD);
    layout(page_w);
    screen = saved;
    log_line("manual: %lu blocks, %d sections, page %dx%d", M.nblocks, M.nsections, page_w, height);
    return 1;
}

// ==========================
// ===  View              ===
// ==========================

static void clamp_top(void) {
    i32 max = (i32)M.page.height - M.view_h;
    if (M.top > max) M.top = max;
    if (M.top < 0) M.top = 0;
}

int manual_open(const char *id, i32 height) {
    if (!M.ready) M.ready = load();
    if (!M.ready) return 0;
    M.view_h = height;
    for (int i = 0; i < M.nsections; i++) {
        if (id && !strncmp(M.sections[i].id, id, strlen(id) + 1)) M.top = M.sections[i].y;
    }
    clamp_top();
    return 1;
}

void manual_draw(void) {
    i32 x0 = ((i32)screen.width - (i32)M.page.width) / 2;
    gfx_fill(0, 0, screen.width, M.view_h, COL_OUTER);
    for (i32 row = 0; row < M.view_h && M.top + row < (i32)M.page.height; row++) {
        memcpy(screen.pixels + (usize)row * screen.width + x0,
               M.page.pixels + (usize)(M.top + row) * M.page.width, M.page.width * 4);
    }
}

void manual_scroll(i32 pixels) {
    M.top += pixels;
    clamp_top();
}

void manual_page(int direction) { manual_scroll(direction * (M.view_h - 3 * gfx_line_height())); }

void manual_edge(int end) {
    M.top = end ? (i32)M.page.height : 0;
    clamp_top();
}

static int current_section(void) {
    int s = 0;
    for (int i = 0; i < M.nsections; i++) {
        if (M.sections[i].y <= M.top + M.view_h / 3) s = i;
    }
    return s;
}

void manual_section_step(int direction) {
    if (!M.nsections) return;
    int s = current_section() + direction;
    if (s < 0) s = 0;
    if (s >= M.nsections) s = M.nsections - 1;
    M.top = M.sections[s].y;
    clamp_top();
}

const char *manual_film(void) { return M.nsections ? M.sections[current_section()].film : 0; }

int manual_percent(void) {
    i32 max = (i32)M.page.height - M.view_h;
    return max > 0 ? (int)((i64)M.top * 100 / max) : 100;
}
