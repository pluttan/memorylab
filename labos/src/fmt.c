#include "fmt.h"

typedef struct {
    char *out;
    usize size;
    usize len;
} sink;

static void put(sink *s, char c) {
    if (s->len + 1 < s->size) s->out[s->len] = c;
    s->len++;
}

static void put_unsigned(sink *s, u64 value, unsigned base, int width, char pad) {
    char digits[24];
    int n = 0;
    do {
        unsigned d = (unsigned)(value % base);
        digits[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        value /= base;
    } while (value);
    while (width-- > n) put(s, pad);
    while (n) put(s, digits[--n]);
}

static void put_double(sink *s, double value, int precision, int width) {
    if (value != value) {
        put(s, 'n'), put(s, 'a'), put(s, 'n');
        return;
    }
    if (value < 0) {
        put(s, '-');
        value = -value;
        width--;
    }
    u64 scale = 1;
    for (int i = 0; i < precision; i++) scale *= 10;
    u64 whole = (u64)(value * (double)scale + 0.5);
    put_unsigned(s, whole / scale, 10, width - (precision ? precision + 1 : 0), ' ');
    if (precision) {
        put(s, '.');
        put_unsigned(s, whole % scale, 10, precision, '0');
    }
}

static void put_string(sink *s, const char *str, int width) {
    usize n = 0;
    while (str[n]) n++;
    while (width-- > (int)n) put(s, ' ');
    while (*str) put(s, *str++);
}

usize vfmt(char *out, usize size, const char *f, va_list ap) {
    sink s = {out, size, 0};
    for (; *f; f++) {
        if (*f != '%') {
            put(&s, *f);
            continue;
        }
        f++;
        char pad = ' ';
        int width = 0, precision = 6, is_long = 0;
        if (*f == '0') pad = '0', f++;
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            precision = 0;
            f++;
            while (*f >= '0' && *f <= '9') precision = precision * 10 + (*f++ - '0');
        }
        while (*f == 'l') is_long = 1, f++;
        switch (*f) {
            case 'd': {
                i64 v = is_long ? va_arg(ap, i64) : va_arg(ap, int);
                if (v < 0) put(&s, '-'), v = -v, width--;
                put_unsigned(&s, (u64)v, 10, width, pad);
                break;
            }
            case 'u': put_unsigned(&s, is_long ? va_arg(ap, u64) : va_arg(ap, unsigned), 10, width, pad); break;
            case 'x': put_unsigned(&s, is_long ? va_arg(ap, u64) : va_arg(ap, unsigned), 16, width, pad); break;
            case 'f': put_double(&s, va_arg(ap, double), precision, width); break;
            case 'c': put(&s, (char)va_arg(ap, int)); break;
            case 's': put_string(&s, va_arg(ap, const char *), width); break;
            case '%': put(&s, '%'); break;
            default: put(&s, '?'); break;
        }
    }
    if (s.size) s.out[s.len < s.size ? s.len : s.size - 1] = 0;
    return s.len;
}

usize fmt(char *out, usize size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    usize n = vfmt(out, size, format, ap);
    va_end(ap);
    return n;
}

u32 utf8_next(const char **s) {
    const u8 *p = (const u8 *)*s;
    u32 c = p[0];
    int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (extra) c &= 0x3f >> extra;
    int i = 1;
    for (; i <= extra && p[i]; i++) c = (c << 6) | (p[i] & 0x3f);
    *s += i;
    return c;
}
