#pragma once
#include <common/irql.h>
#include <stdarg.h>
#include <stddef.h>

void term_init(void);

int snvprintf(char* buffer, size_t bufsz, const char* fmt, va_list val);
int snprintf(char* buffer, size_t bufsz, const char* fmt, ...);
int vprintf(const char* fmt, va_list val);
int printf(const char* fmt, ...);

void stdio_lock();
void stdio_unlock();

int nl_vprintf(const char* fmt, va_list val);
int nl_printf(const char* fmt, ...);

#define COLORIZE(text, color) "\x1b[1m\x1b[" color "m" text "\x1b[0m"

#define LOG_FAIL(fmt, ...)                                                \
    do { printf(COLORIZE("fail | ", "91") fmt, ##__VA_ARGS__); } while(0)

#define LOG_WARN(fmt, ...)                                                \
    do { printf(COLORIZE("warn | ", "93") fmt, ##__VA_ARGS__); } while(0)

#define LOG_OKAY(fmt, ...)                                                \
    do { printf(COLORIZE("okay | ", "92") fmt, ##__VA_ARGS__); } while(0)

#define LOG_INFO(fmt, ...)                                                \
    do { printf(COLORIZE("info | ", "96") fmt, ##__VA_ARGS__); } while(0)
