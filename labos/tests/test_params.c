// Host tests of parameter stepping and editing.
#include <stdio.h>

#include "params.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

static void test_step(void) {
    exp_param rounds = {"Повторов", "", 1, 63, 15, 0};
    param_step(&rounds, +1);
    CHECK(rounds.value == 16);
    rounds.value = 63;
    param_step(&rounds, +1);
    CHECK(rounds.value == 63);  // stays at the top of the range
    rounds.value = 1;
    param_step(&rounds, -1);
    CHECK(rounds.value == 1);

    exp_param block = {"Блок", "МБ", 2, 16, 2, 1};
    param_step(&block, +1);
    CHECK(block.value == 4);
    param_step(&block, +1), param_step(&block, +1), param_step(&block, +1);
    CHECK(block.value == 16);
    param_step(&block, -1);
    CHECK(block.value == 8);
    block.value = 2;
    param_step(&block, -1);
    CHECK(block.value == 2);
}

static void test_edit(void) {
    exp_param lines = {"Линеек", "", 2, 512, 512, 1};
    param_edit e = {{0}, 0};
    edit_digit(&e, '0');
    edit_digit(&e, '6');
    edit_digit(&e, '4');
    CHECK(e.length == 2);  // the leading zero went away
    CHECK(edit_commit(&e, &lines) && lines.value == 64 && e.length == 0);
    CHECK(!edit_commit(&e, &lines) && lines.value == 64);  // nothing typed keeps the value
    edit_digit(&e, '9'), edit_digit(&e, '9'), edit_digit(&e, '9');
    edit_commit(&e, &lines);
    CHECK(lines.value == 512);  // clamped
    edit_digit(&e, '1');
    edit_commit(&e, &lines);
    CHECK(lines.value == 2);
    edit_digit(&e, '3'), edit_digit(&e, '2');
    edit_backspace(&e);
    edit_commit(&e, &lines);
    CHECK(lines.value == 3);
    edit_digit(&e, 'x');
    CHECK(e.length == 0);
    for (int i = 0; i < 20; i++) edit_digit(&e, '9');
    CHECK(e.length == 11);  // the buffer does not overflow
}

int main(void) {
    test_step();
    test_edit();
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
