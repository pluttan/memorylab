// Experiment parameters and their editing in the menu. Pure: tests run it on the host.
#ifndef LABOS_PARAMS_H
#define LABOS_PARAMS_H

#include "efi.h"

typedef struct {
    const char *name;
    const char *unit;
    i64 min, max, value;
    int doubling;  // Left and Right halve and double the value instead of stepping by one
} exp_param;

// Typed digits before Enter, Tab or an arrow key confirms them
typedef struct {
    char text[12];
    int length;
} param_edit;

i64 param_clamp(const exp_param *p, i64 value);
void param_step(exp_param *p, int direction);

void edit_digit(param_edit *e, char digit);
void edit_backspace(param_edit *e);
// Moves the typed value into the parameter, clamped to its range; returns 1 when there was one
int edit_commit(param_edit *e, exp_param *p);
void edit_cancel(param_edit *e);

#endif
