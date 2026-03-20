#pragma once
#include <stdint.h>

typedef enum irql : uint8_t {
    IRQL_PASSIVE = 0,
    IRQL_DISPATCH = 2,
    IRQL_HIGH = 15
} irql_t;

// Raises the IRQL to the next higher level
// @returns: the previous IRQL level
[[nodiscard]] irql_t irql_raise(irql_t new_level);

// Lowers the IRQL to the next lower level
// @returns: the previous IRQL level
irql_t irql_lower(irql_t new_level);

// Gets the current IRQL level
// @returns: the current IRQL level
[[nodiscard]] irql_t irql_get();
