// The manual file made by tools/mkmanual.py: one block per line, a tag, a space, the text. Pure: tests run it.
#ifndef LABOS_MANUALFMT_H
#define LABOS_MANUALFMT_H

#include "efi.h"

typedef enum {
    MB_SECTION,
    MB_H1,
    MB_H2,
    MB_H3,
    MB_PARA,
    MB_ITEM,
    MB_QUOTE,
    MB_FORMULA,
    MB_CODE,
    MB_TABLE_HEAD,
    MB_TABLE_ROW,
    MB_FILM,
    MB_GAP,
} mblock_kind;

typedef struct {
    mblock_kind kind;
    const char *text;  // ends with a zero byte
} mblock;

// Text emphasis inside blocks: \x01 starts it, \x02 ends it
#define EMPH_ON 1
#define EMPH_OFF 2

// Splits the text into blocks in place (line ends become zero bytes); unknown tags are skipped.
// Returns the number of blocks, at most cap
usize manual_parse(char *text, usize size, mblock *out, usize cap);

// Splits a table row at tabs in place; returns the number of cells, at most cap
int manual_cells(char *row, const char **cells, int cap);

#endif
