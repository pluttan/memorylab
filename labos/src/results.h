// Results on the stick: after every run the report, a screenshot and the log go to labos/results.
#ifndef LABOS_RESULTS_H
#define LABOS_RESULTS_H

#include "efi.h"

// Saves <id>-<date>-<time>.txt (the report) and .bmp (the screen) and labos/log.txt.
// saved gets the name of the report for the screen; returns 1 when everything was written
int results_save(const char *id, usize (*report)(u8 *out, usize cap), char *saved, usize size);

#endif
