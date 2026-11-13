// labos: memory experiments on bare hardware, graphs on the screen, control over the network.
#include "exp.h"
#include "gfx.h"
#include "lib.h"
#include "net.h"
#include "remote.h"
#include "ui.h"

#define EFI_RESET_COLD 0

// ==========================
// ===  Network commands  ===
// ==========================

static const char *next_word(const char **s, char *word, usize size) {
    while (**s == ' ') (*s)++;
    usize n = 0;
    while (**s && **s != ' ' && n + 1 < size) word[n++] = *(*s)++;
    while (**s && **s != ' ') (*s)++;
    word[n] = 0;
    return n ? word : 0;
}

// "run <id> [value ...]": the values go to the parameters in order, clamped to their ranges
static usize cmd_run(const char *args, u8 *out, usize cap) {
    char word[32];
    if (!next_word(&args, word, sizeof word)) return fmt((char *)out, cap, "error: run <experiment> [values]");
    int index = ui_find(word);
    if (index < 0) return fmt((char *)out, cap, "error: unknown experiment '%s'", word);
    experiment *e = &experiments[index];
    for (int i = 0; i < e->nparams && next_word(&args, word, sizeof word); i++) {
        i64 v = 0;
        for (const char *c = word; *c >= '0' && *c <= '9'; c++) v = v * 10 + (*c - '0');
        e->params[i].value = param_clamp(&e->params[i], v);
    }
    if (!ui_run(index)) return fmt((char *)out, cap, "error: %s did not run", e->id);
    return e->report(out, cap);
}

// "key <name> [name ...]": presses keys as if on the keyboard, for remote checks of the menu
static usize cmd_key(const char *args, u8 *out, usize cap) {
    char word[16];
    int pressed = 0;
    while (next_word(&args, word, sizeof word)) {
        ui_key k = ui_key_from_name(word);
        if (k.kind == KEY_NONE) return fmt((char *)out, cap, "error: unknown key '%s'", word);
        ui_handle(k);
        pressed++;
    }
    return fmt((char *)out, cap, "ok %d", pressed);
}

// "menu": every experiment with its parameters
static usize cmd_menu(const char *args, u8 *out, usize cap) {
    (void)args;
    char *s = (char *)out;
    usize n = 0;
    for (int i = 0; i < experiment_count && n + 256 < cap; i++) {
        const experiment *e = &experiments[i];
        n += fmt(s + n, cap - n, "%s%s: %s\n", e->id, e->run ? "" : " (notebook only)", e->title);
        for (int k = 0; k < e->nparams; k++) {
            const exp_param *p = &e->params[k];
            n += fmt(s + n, cap - n, "    %s = %ld %s [%ld..%ld]\n", p->name, p->value, p->unit, p->min, p->max);
        }
    }
    return n;
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
    ui_init();
    if (net_init()) {
        if (remote_init()) {
            remote_register("run", cmd_run);
            remote_register("key", cmd_key);
            remote_register("menu", cmd_menu);
        } else {
            log_line("labos: no memory for the remote buffer");
        }
    }
    log_line("labos: ready");
    u64 reset_at = 0;
    for (;;) {
        net_poll();
        ui_tick();
        efi_input_key key;
        if (ST->con_in->read_key_stroke(ST->con_in, &key) == EFI_SUCCESS) {
            ui_handle(ui_key_from_efi(key.scan_code, key.unicode_char));
        }
        if ((remote_reset_requested || ui_reset_requested) && !reset_at) reset_at = now_us() + 200000;
        if (reset_at && now_us() > reset_at) {
            log_line("labos: reset");
            ST->runtime_services->reset_system(EFI_RESET_COLD, EFI_SUCCESS, 0, 0);
        }
        BS->stall(100);
    }
}
