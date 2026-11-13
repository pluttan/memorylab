// The lab manual from labos/manual.txt: laid out once into a tall page, then scrolled.
#ifndef LABOS_MANUAL_H
#define LABOS_MANUAL_H

#include "efi.h"

// Loads and lays out the manual on first use, then shows the section (cpuinfo, rowconf, ..., intro,
// questions) within the top `height` pixels; 0 when the file is missing (manual_error says why)
int manual_open(const char *section, i32 height);
const char *manual_error(void);

void manual_scroll(i32 pixels);
void manual_page(int direction);
void manual_edge(int end);
void manual_section_step(int direction);
void manual_draw(void);

// The film of the section at the top of the view, or 0
const char *manual_film(void);
int manual_percent(void);

#endif
