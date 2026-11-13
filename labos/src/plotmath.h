// Axis arithmetic without libm. Pure functions: tests/test_plotmath.c runs them on the host.
#ifndef LABOS_PLOTMATH_H
#define LABOS_PLOTMATH_H

#include "efi.h"

// Tick step for [lo, hi] with at most `target` ticks: 1, 2 or 5 times a power of ten
double nice_step(double lo, double hi, int target);

// Tick step for a byte axis: a power of two, so that the labels are round sizes (256 КБ, not 244 КБ)
double nice_step_bytes(double lo, double hi, int target);

// Digits after the point needed to print ticks of this step
int step_decimals(double step);

// Base 2 logarithm, accurate to about 1e-6 for positive finite values
double lb(double v);

// Position of v on an axis from p0 (value v0) to p1 (value v1); log2 axes take lb of both
double axis_pos(double v, double v0, double v1, int log2, double p0, double p1);

// "512 Б", "32 КБ", "1,25 МБ": sizes on the axes, with a decimal comma and up to two decimals
void format_bytes(char *out, unsigned size, double bytes);

// A count with spaces between thousands: "131 072"
void format_count(char *out, unsigned size, u64 value);

// A number with `decimals` digits after a decimal comma
void format_number(char *out, unsigned size, double value, int decimals);

#endif
