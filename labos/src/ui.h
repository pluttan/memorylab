// The menu in the manner of the PCLAB window: experiments on the left, description and parameters on
// the right, the result on its own screen. Keyboard only: the firmware gives no mouse everywhere.
#ifndef LABOS_UI_H
#define LABOS_UI_H

#include "efi.h"

typedef enum {
    KEY_NONE,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_TAB,
    KEY_ENTER,
    KEY_ESC,
    KEY_BACKSPACE,
    KEY_F10,
    KEY_CHAR,
} key_kind;

typedef struct {
    key_kind kind;
    char ch;  // for KEY_CHAR
} ui_key;

ui_key ui_key_from_efi(u16 scan_code, u16 unicode);
// "up", "down", "left", "right", "tab", "enter", "esc", "backspace", "f10" or one character
ui_key ui_key_from_name(const char *name);

void ui_init(void);
void ui_handle(ui_key key);
// Redraws the status line when the network address changes
void ui_tick(void);

int ui_find(const char *id);
// Runs experiment `index` with its current parameters and shows the result; 0 when it could not run
int ui_run(int index);

extern int ui_reset_requested;

#endif
