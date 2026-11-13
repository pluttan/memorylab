#include "plotmath.h"

#include "fmt.h"

double nice_step(double lo, double hi, int target) {
    double range = hi - lo;
    if (range <= 0 || target < 1) return 1;
    double raw = range / target;
    double power = 1;
    while (power * 10 <= raw) power *= 10;
    while (power > raw) power /= 10;
    // power <= raw < 10 * power; pick the smallest of 1, 2, 5, 10 times power that is not below raw
    static const double factors[] = {1, 2, 5, 10};
    for (int i = 0; i < 4; i++) {
        if (factors[i] * power >= raw) return factors[i] * power;
    }
    return 10 * power;
}

double nice_step_bytes(double lo, double hi, int target) {
    double range = hi - lo;
    if (range <= 0 || target < 1) return 1;
    double raw = range / target, step = 1;
    while (step < raw) step *= 2;
    return step;
}

int step_decimals(double step) {
    int decimals = 0;
    while (decimals < 6) {
        double scaled = step;
        for (int i = 0; i < decimals; i++) scaled *= 10;
        double frac = scaled - (double)(long long)scaled;
        if (frac < 1e-6 || frac > 1 - 1e-6) break;
        decimals++;
    }
    return decimals;
}

double lb(double v) {
    if (v <= 0) return -1e300;
    int exponent = 0;
    while (v >= 2) v /= 2, exponent++;
    while (v < 1) v *= 2, exponent--;
    // v in [1, 2): ln(v) = 2 atanh((v - 1) / (v + 1)), the series converges fast for t <= 1/3
    double t = (v - 1) / (v + 1), t2 = t * t, term = t, sum = 0;
    for (int k = 1; k < 40; k += 2) {
        sum += term / k;
        term *= t2;
    }
    return exponent + 2 * sum / 0.6931471805599453;
}

double axis_pos(double v, double v0, double v1, int log2, double p0, double p1) {
    if (log2) v = lb(v), v0 = lb(v0), v1 = lb(v1);
    if (v1 == v0) return p0;
    return p0 + (v - v0) / (v1 - v0) * (p1 - p0);
}

void format_count(char *out, unsigned size, u64 value) {
    char digits[24];
    usize n = fmt(digits, sizeof digits, "%lu", value), o = 0;
    for (usize i = 0; i < n && o + 2 < size; i++) {
        if (i && (n - i) % 3 == 0) out[o++] = ' ';
        out[o++] = digits[i];
    }
    if (size) out[o < size ? o : size - 1] = 0;
}

void format_number(char *out, unsigned size, double value, int decimals) {
    char spec[8];
    fmt(spec, sizeof spec, "%%.%df", decimals);
    usize n = fmt(out, size, spec, value);
    for (usize i = 0; i < n && i < size; i++) {
        if (out[i] == '.') out[i] = ',';
    }
}

void format_bytes(char *out, unsigned size, double bytes) {
    static const char *units[] = {"Б", "КБ", "МБ", "ГБ"};
    int unit = 0;
    while (bytes >= 1024 && unit < 3) bytes /= 1024, unit++;
    long long hundredths = (long long)(bytes * 100 + 0.5);
    int decimals = hundredths % 100 == 0 ? 0 : hundredths % 10 == 0 ? 1 : 2;
    char number[24];
    format_number(number, sizeof number, (double)hundredths / 100, decimals);
    fmt(out, size, "%s %s", number, units[unit]);
}
