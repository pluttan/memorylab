#include "ui.h"

#include "exp.h"
#include "gfx.h"
#include "lib.h"
#include "net.h"
#include "plotmath.h"
#include "widgets.h"

#define PAD 24
#define INSET 28
#define COL_BOX 0x1e1e2e  // Catppuccin base: value boxes inside the field

typedef enum { MODE_MENU, MODE_RESULT } ui_mode;

int ui_reset_requested;

static struct {
    ui_mode mode;
    int selected;
    int focus_params;
    int row;
    param_edit edit;
    const char *message;
    u32 message_color;
    char status[192];
} U;

// ==========================
// ===  Keys              ===
// ==========================

ui_key ui_key_from_efi(u16 scan, u16 ch) {
    switch (scan) {
        case 0x01: return (ui_key){KEY_UP, 0};
        case 0x02: return (ui_key){KEY_DOWN, 0};
        case 0x03: return (ui_key){KEY_RIGHT, 0};
        case 0x04: return (ui_key){KEY_LEFT, 0};
        case 0x14: return (ui_key){KEY_F10, 0};
        case 0x17: return (ui_key){KEY_ESC, 0};
    }
    if (ch == '\r' || ch == '\n') return (ui_key){KEY_ENTER, 0};
    if (ch == '\t') return (ui_key){KEY_TAB, 0};
    if (ch == 8) return (ui_key){KEY_BACKSPACE, 0};
    if (ch >= 0x20 && ch < 0x7f) return (ui_key){KEY_CHAR, (char)ch};
    return (ui_key){KEY_NONE, 0};
}

ui_key ui_key_from_name(const char *name) {
    static const struct {
        const char *name;
        key_kind kind;
    } names[] = {{"up", KEY_UP},       {"down", KEY_DOWN},           {"left", KEY_LEFT}, {"right", KEY_RIGHT},
                 {"tab", KEY_TAB},     {"enter", KEY_ENTER},         {"esc", KEY_ESC},   {"backspace", KEY_BACKSPACE},
                 {"f10", KEY_F10}};
    for (usize i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (!strncmp(name, names[i].name, strlen(names[i].name) + 1)) return (ui_key){names[i].kind, 0};
    }
    if (name[0] && !name[1]) return (ui_key){KEY_CHAR, name[0]};
    return (ui_key){KEY_NONE, 0};
}

// ==========================
// ===  Status line       ===
// ==========================

static i32 status_height(void) { return gfx_line_height() + 12; }

static const char *hints(void) {
    if (U.mode == MODE_RESULT) return "Esc: в меню    Enter: повторить опыт    F10: перезагрузка";
    if (U.focus_params) {
        return "Вверх/вниз: параметр    Цифры: ввод    Влево/вправо: меньше/больше    Enter: запуск    Tab: назад";
    }
    return "Вверх/вниз: опыт    Tab: параметры    Enter: запуск    F10: перезагрузка";
}

static void draw_status(void) {
    i32 h = status_height(), y = screen.height - h;
    char ip[32], right[64];
    net_status(ip, sizeof ip);
    fmt(right, sizeof right, "labos · %s", ip);
    gfx_fill(0, y, screen.width, h, COL_GRID);
    gfx_text(16, y + 6, hints(), COL_TEXT);
    gfx_text(screen.width - 16 - gfx_text_width(right), y + 6, right, COL_SUBTEXT);
    fmt(U.status, sizeof U.status, "%s|%s", hints(), right);
}

void ui_tick(void) {
    char ip[32], right[64], now[192];
    net_status(ip, sizeof ip);
    fmt(right, sizeof right, "labos · %s", ip);
    fmt(now, sizeof now, "%s|%s", hints(), right);
    if (!strncmp(now, U.status, sizeof now)) return;
    draw_status();
    gfx_present();
}

// ==========================
// ===  Menu              ===
// ==========================

static i32 draw_list(i32 x, i32 y, i32 h) {
    i32 lh = gfx_line_height(), w = 0;
    for (int i = 0; i < experiment_count; i++) {
        i32 tw = gfx_text_width(experiments[i].short_title);
        if (tw > w) w = tw;
    }
    w += 2 * INSET;
    gfx_fill(x, y, w, h, COL_FIELD);
    gfx_text(x + INSET, y + 18, "Опыты", COL_SUBTEXT);
    i32 iy = y + 18 + lh + 14, item_h = lh + 16;
    for (int i = 0; i < experiment_count; i++, iy += item_h) {
        if (i == U.selected) {
            gfx_fill(x, iy, w, item_h, COL_GRID);
            gfx_fill(x, iy, 4, item_h, U.focus_params ? COL_AXIS : COL_MAUVE);
        }
        u32 color = experiments[i].run ? COL_TEXT : COL_AXIS;
        gfx_text(x + INSET, iy + 8, experiments[i].short_title, color);
    }
    return w;
}

static void range_text(char *out, usize size, const exp_param *p) {
    char lo[24], hi[24];
    format_count(lo, sizeof lo, (u64)p->min);
    format_count(hi, sizeof hi, (u64)p->max);
    fmt(out, size, "от %s до %s%s%s", lo, hi, p->unit[0] ? " " : "", p->unit);
}

static i32 draw_params(const experiment *e, i32 x, i32 y, i32 width) {
    i32 lh = gfx_line_height(), row_h = lh + 18, top = y;
    if (!e->nparams) {
        gfx_text(x, y, "Параметров нет.", COL_SUBTEXT);
        return lh;
    }
    i32 name_w = 0;
    for (int i = 0; i < e->nparams; i++) {
        i32 w = gfx_text_width(e->params[i].name);
        if (w > name_w) name_w = w;
    }
    i32 box_x = x + name_w + 28, box_w = 170;
    for (int i = 0; i < e->nparams; i++, y += row_h) {
        const exp_param *p = &e->params[i];
        int current = U.focus_params && i == U.row;
        if (current) gfx_fill(x - 12, y, width + 24, row_h, COL_GRID);
        gfx_text(x, y + 9, p->name, COL_TEXT);
        gfx_fill(box_x, y + 4, box_w, row_h - 8, current ? COL_MAUVE : COL_AXIS);
        gfx_fill(box_x + 2, y + 6, box_w - 4, row_h - 12, COL_BOX);
        char value[32];
        int typing = current && U.edit.length;
        if (typing) {
            fmt(value, sizeof value, "%s", U.edit.text);
        } else {
            format_count(value, sizeof value, (u64)p->value);
        }
        i32 vw = gfx_text(box_x + 12, y + 9, value, typing ? COL_PEACH : COL_TEXT);
        if (current) gfx_fill(box_x + 14 + vw, y + 10, 2, lh - 2, COL_MAUVE);
        char range[64];
        range_text(range, sizeof range, p);
        gfx_text(box_x + box_w + 20, y + 9, range, COL_SUBTEXT);
    }
    return y - top;
}

static void draw_menu(void) {
    i32 lh = gfx_line_height(), bottom = screen.height - status_height() - PAD;
    gfx_fill(0, 0, screen.width, screen.height, COL_OUTER);
    gfx_text(PAD, 16, "Организация памяти конвейерных суперскалярных ЭВМ", COL_TEXT);
    i32 y = 16 + lh + 16, h = bottom - y;
    i32 list_w = draw_list(PAD, y, h);
    i32 rx = PAD + list_w + PAD, rw = screen.width - rx - PAD;
    gfx_fill(rx, y, rw, h, COL_FIELD);
    const experiment *e = &experiments[U.selected];
    i32 x = rx + INSET, width = rw - 2 * INSET, ty = y + 18;
    gfx_text(x, ty, e->title, COL_TEXT);
    ty += lh + 4;
    gfx_text(x, ty, e->tasks, COL_MAUVE);
    ty += lh + 18;
    ty += wrap_text(x, ty, width, e->about, COL_SUBTEXT) + 18;
    gfx_text(x, ty, "Параметры", COL_MAUVE);
    ty += lh + 10;
    ty += draw_params(e, x, ty, width) + 24;
    if (e->run) {
        const char *run = "Enter: запустить";
        i32 bw = gfx_text_width(run) + 40;
        gfx_fill(x, ty, bw, lh + 16, COL_GREEN);
        gfx_fill(x + 2, ty + 2, bw - 4, lh + 12, COL_BOX);
        gfx_text(x + 20, ty + 8, run, COL_GREEN);
        ty += lh + 16;
    } else {
        ty += wrap_text(x, ty, width, "Этот опыт ещё не перенесён в labos. Пока проведите его в тетради memorylab.",
                        COL_PEACH);
    }
    if (U.message) wrap_text(x, ty + 16, width, U.message, U.message_color);
    draw_status();
}

static void redraw(void) {
    if (U.mode == MODE_RESULT) {
        experiments[U.selected].draw(screen.height - status_height());
        draw_status();
    } else {
        draw_menu();
    }
    gfx_present();
}

void ui_init(void) {
    memset(&U, 0, sizeof U);
    redraw();
}

int ui_find(const char *id) {
    for (int i = 0; i < experiment_count; i++) {
        if (!strncmp(id, experiments[i].id, strlen(experiments[i].id) + 1)) return i;
    }
    return -1;
}

static void select_experiment(int index);

int ui_run(int index) {
    experiment *e = &experiments[index];
    if (index != U.selected) {
        select_experiment(index);
    } else if (e->nparams) {
        edit_commit(&U.edit, &e->params[U.row]);
    }
    if (!e->run) {
        U.mode = MODE_MENU;
        U.message = "Этот опыт пока проводится только в тетради memorylab.";
        U.message_color = COL_PEACH;
        redraw();
        return 0;
    }
    U.mode = MODE_MENU;
    U.message = "Идёт опыт, подождите…";
    U.message_color = COL_PEACH;
    redraw();
    if (!e->run(e->params)) {
        U.message = "Опыту не хватило памяти.";
        U.message_color = COL_RED;
        redraw();
        return 0;
    }
    U.message = 0;
    U.mode = MODE_RESULT;
    redraw();
    return 1;
}

// ==========================
// ===  Key handling      ===
// ==========================

static void select_experiment(int index) {
    if (index < 0 || index >= experiment_count) return;
    edit_cancel(&U.edit);
    U.selected = index;
    U.row = 0;
    U.message = 0;
    if (!experiments[index].nparams) U.focus_params = 0;
}

static void key_in_list(ui_key k) {
    if (k.kind == KEY_UP) select_experiment(U.selected - 1);
    if (k.kind == KEY_DOWN) select_experiment(U.selected + 1);
    if ((k.kind == KEY_TAB || k.kind == KEY_RIGHT) && experiments[U.selected].nparams) U.focus_params = 1;
    if (k.kind == KEY_CHAR && k.ch >= '0' && k.ch <= '9') select_experiment(k.ch - '0');
}

static void key_in_params(ui_key k) {
    experiment *e = &experiments[U.selected];
    exp_param *p = &e->params[U.row];
    switch (k.kind) {
        case KEY_UP:
        case KEY_DOWN:
            edit_commit(&U.edit, p);
            U.row += k.kind == KEY_UP ? -1 : 1;
            if (U.row < 0) U.row = 0;
            if (U.row >= e->nparams) U.row = e->nparams - 1;
            break;
        case KEY_LEFT:
        case KEY_RIGHT:
            edit_commit(&U.edit, p);
            param_step(p, k.kind == KEY_RIGHT ? 1 : -1);
            break;
        case KEY_BACKSPACE: edit_backspace(&U.edit); break;
        case KEY_CHAR: edit_digit(&U.edit, k.ch); break;
        case KEY_TAB: edit_commit(&U.edit, p), U.focus_params = 0; break;
        case KEY_ESC:
            // Esc drops the typed digits first, then leaves the table
            if (U.edit.length) {
                edit_cancel(&U.edit);
            } else {
                U.focus_params = 0;
            }
            break;
        default: break;
    }
}

void ui_handle(ui_key k) {
    if (k.kind == KEY_NONE) return;
    if (k.kind == KEY_F10) {
        ui_reset_requested = 1;
        return;
    }
    if (k.kind == KEY_ENTER) {
        ui_run(U.selected);
        return;
    }
    if (U.mode == MODE_RESULT) {
        if (k.kind == KEY_ESC || k.kind == KEY_BACKSPACE) U.mode = MODE_MENU;
    } else if (U.focus_params) {
        key_in_params(k);
    } else {
        key_in_list(k);
    }
    redraw();
}
