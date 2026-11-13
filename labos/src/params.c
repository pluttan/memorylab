#include "params.h"

i64 param_clamp(const exp_param *p, i64 value) {
    if (value < p->min) return p->min;
    if (value > p->max) return p->max;
    return value;
}

void param_step(exp_param *p, int direction) {
    i64 v = p->value;
    if (p->doubling) {
        v = direction > 0 ? (v > 0 ? v * 2 : 1) : v / 2;
    } else {
        v += direction > 0 ? 1 : -1;
    }
    p->value = param_clamp(p, v);
}

void edit_digit(param_edit *e, char digit) {
    if (digit < '0' || digit > '9' || e->length >= (int)sizeof e->text - 1) return;
    if (e->length == 1 && e->text[0] == '0') e->length = 0;  // no leading zeros
    e->text[e->length++] = digit;
    e->text[e->length] = 0;
}

void edit_backspace(param_edit *e) {
    if (e->length > 0) e->text[--e->length] = 0;
}

int edit_commit(param_edit *e, exp_param *p) {
    if (!e->length) return 0;
    i64 v = 0;
    for (int i = 0; i < e->length; i++) v = v * 10 + (e->text[i] - '0');
    p->value = param_clamp(p, v);
    edit_cancel(e);
    return 1;
}

void edit_cancel(param_edit *e) {
    e->length = 0;
    e->text[0] = 0;
}
