// An experiment as the menu sees it: texts, parameters and three entry points.
#ifndef LABOS_EXP_H
#define LABOS_EXP_H

#include "params.h"

#define MAX_PARAMS 4

typedef struct {
    const char *id;           // name in network commands: "run rowconf 2 15"
    const char *short_title;  // line in the menu list
    const char *title;
    const char *tasks;        // tasks of the lab manual it serves
    const char *about;
    const char *film;         // labos/films/<film>-<height>.lfilm, or 0
    exp_param params[MAX_PARAMS];
    int nparams;
    // Measures and keeps the result; 0 when it could not run (no memory)
    int (*run)(const exp_param *params);
    // Draws the kept result into the top `height` pixels of the screen
    void (*draw)(i32 height);
    // The kept result as text for the network
    usize (*report)(u8 *out, usize cap);
} experiment;

extern experiment experiments[];
extern const int experiment_count;

#endif
