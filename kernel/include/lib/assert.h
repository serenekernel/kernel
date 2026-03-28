#pragma once
#include <common/arch.h>
#include <stdio.h>

#define assert(expr)                                                                                                    \
    do {                                                                                                                \
        if(!(expr)) {                                                                                                   \
            nl_printf(COLORIZE("fail | ", "91") "Assertion failed: %s, file %s, line %d\n", #expr, __FILE__, __LINE__); \
            arch_die();                                                                                                 \
        }                                                                                                               \
    } while(0)

#define assertf(expr, fmt, ...)                                                                                                                 \
    do {                                                                                                                                        \
        if(!(expr)) {                                                                                                                           \
            nl_printf(COLORIZE("fail | ", "91") "Assertion failed: %s, file %s, line %d: " fmt "\n", #expr, __FILE__, __LINE__, ##__VA_ARGS__); \
            arch_die();                                                                                                                         \
        }                                                                                                                                       \
    } while(0)

#define EXPECT_LIKELY(V) (__builtin_expect(!!(V), 1))
#define EXPECT_UNLIKELY(V) (__builtin_expect(!!(V), 0))
