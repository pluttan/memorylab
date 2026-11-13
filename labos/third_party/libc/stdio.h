#ifndef LABOS_LIBC_STDIO_H
#define LABOS_LIBC_STDIO_H
// h264bsd prints debug output only in debug builds; its error messages (_ERROR_PRINT) go to the labos log
void labos_log_printf(const char *format, ...);
#define stderr 0
#define printf(...) ((void)0)
#define fprintf(stream, ...) labos_log_printf(__VA_ARGS__)
#endif
