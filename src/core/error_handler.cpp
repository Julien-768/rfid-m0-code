/**
 * @file error_handler.cpp
 * @brief Implementation of the LED-based error signaling system.
 *
 * This file implements the runtime behavior of the error signaling mechanism
 * declared in @ref error_handler.h.
 *
 * Errors are reported using LED blink patterns corresponding to the numeric
 * value of the @ref ErrorCode. Optionally, the system may halt indefinitely
 * after signaling a fatal error.
 *
 * This module is designed for last-resort diagnostics when logging or
 * serial communication may not be available.
 *
 * @see error_handler.h
 * @ingroup SystemModules
 */

#include <Arduino.h>

#include "error_handler.h"
#include "log.h"
#include "signal.h"

namespace {
/// Return a safe, non-negative blink count from an ErrorCode.
inline uint8_t blink_count(ErrorCode code) {
    const int v = static_cast<int>(code);
    return (v > 0) ? static_cast<uint8_t>(v) : 0u;
}
}  // namespace

/**
 * @brief Log and signal an error using LED blink patterns and optional system halt.
 *
 * The error code is represented by a number of LED blinks equal to the
 * numeric value of @p code.
 *
 * If @p halt_system is true, the function does not return and the system
 * remains halted after signaling the error.
 *
 * @param code         Error code to signal.
 */
void error_signal(ErrorCode code) {
    LOG_ERROR("Error code %d", static_cast<int>(code));
    // led_start_blink_isr(blink_count(code), blink_mode::fast);
}
