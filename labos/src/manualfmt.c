#include "manualfmt.h"

static const struct {
    const char *tag;
    mblock_kind kind;
} TAGS[] = {
    {"S", MB_SECTION}, {"H1", MB_H1},     {"H2", MB_H2},         {"H3", MB_H3},        {"P", MB_PARA},
    {"L", MB_ITEM},    {"Q", MB_QUOTE},   {"F", MB_FORMULA},     {"C", MB_CODE},       {"TH", MB_TABLE_HEAD},
    {"TR", MB_TABLE_ROW}, {"V", MB_FILM}, {"G", MB_GAP},
};

static int tag_is(const char *line, usize length, const char *tag) {
    usize n = 0;
    while (tag[n]) n++;
    if (length < n) return 0;
    for (usize i = 0; i < n; i++) {
        if (line[i] != tag[i]) return 0;
    }
    return length == n || line[n] == ' ';
}

usize manual_parse(char *text, usize size, mblock *out, usize cap) {
    usize count = 0, start = 0;
    while (start < size && count < cap) {
        usize end = start;
        while (end < size && text[end] != '\n') end++;
        usize length = end - start;
        if (end < size) text[end] = 0;
        char *line = text + start;
        for (usize t = 0; t < sizeof TAGS / sizeof TAGS[0]; t++) {
            if (!tag_is(line, length, TAGS[t].tag)) continue;
            usize skip = 0;
            while (TAGS[t].tag[skip]) skip++;
            out[count].kind = TAGS[t].kind;
            out[count].text = line + (skip < length ? skip + 1 : length);
            count++;
            break;
        }
        start = end + 1;
    }
    return count;
}

int manual_cells(char *row, const char **cells, int cap) {
    int n = 0;
    if (cap < 1) return 0;
    cells[n++] = row;
    for (char *p = row; *p; p++) {
        if (*p != '\t') continue;
        *p = 0;
        if (n == cap) break;
        cells[n++] = p + 1;
    }
    return n;
}
