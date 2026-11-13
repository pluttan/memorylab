// Host tests of the manual format.
#include <stdio.h>
#include <string.h>

#include "manualfmt.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

int main(void) {
    char text[] = "S rowconf\nH2 Опыт 1\nP Банк \x01закрывает\x02 строку\nC     x += 1;\nC\n"
                  "TH №\tДиапазон\tОписание\nTR 1\t2..16 МБ\tРазмер блока\nV exp1\nG\nZZ unknown\nH3";
    mblock b[16];
    usize n = manual_parse(text, strlen(text), b, 16);
    CHECK(n == 10);
    CHECK(b[0].kind == MB_SECTION && !strcmp(b[0].text, "rowconf"));
    CHECK(b[1].kind == MB_H2 && !strcmp(b[1].text, "Опыт 1"));
    CHECK(b[2].kind == MB_PARA && strchr(b[2].text, 1) && strchr(b[2].text, 2));
    CHECK(b[3].kind == MB_CODE && !strcmp(b[3].text, "    x += 1;"));  // indentation kept
    CHECK(b[4].kind == MB_CODE && !strcmp(b[4].text, ""));             // empty code line
    CHECK(b[5].kind == MB_TABLE_HEAD && b[6].kind == MB_TABLE_ROW);
    CHECK(b[7].kind == MB_FILM && !strcmp(b[7].text, "exp1"));
    CHECK(b[8].kind == MB_GAP);
    CHECK(b[9].kind == MB_H3 && !strcmp(b[9].text, ""));  // the last line has no line end
    const char *cells[4];
    char row[64];
    strcpy(row, b[6].text);
    CHECK(manual_cells(row, cells, 4) == 3);
    CHECK(!strcmp(cells[0], "1") && !strcmp(cells[1], "2..16 МБ") && !strcmp(cells[2], "Размер блока"));
    strcpy(row, "a\tb\tc\td\te");
    CHECK(manual_cells(row, cells, 2) == 2 && !strcmp(cells[1], "b"));
    char again[] = "P a\nP b\nP c\nP d\n";
    CHECK(manual_parse(again, strlen(again), b, 3) == 3);  // stops at cap
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
