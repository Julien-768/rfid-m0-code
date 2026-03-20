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

#include "core/error_handler.h"
#include "core/log.h"

namespace
{
/// LED blink ON duration (ms).
constexpr uint16_t kBlinkOnMs = 200;

/// LED blink OFF duration (ms).
constexpr uint16_t kBlinkOffMs = 200;

/// Pause after a full code sequence (ms).
constexpr uint16_t kSequenceGapMs = 800;

/// Return a safe, non-negative blink count from an ErrorCode.
inline uint8_t blink_count(ErrorCode code) {
    const int v = static_cast<int>(code);
    return (v > 0) ? static_cast<uint8_t>(v) : 0u;
}
}  // namespace

/**
 * @brief Signal an error using LED blink patterns and optional system halt.
 *
 * The error code is represented by a number of LED blinks equal to the
 * numeric value of @p code.
 *
 * @note On Feather M0-class boards, @c LED_BUILTIN is typically active-high
 *       (HIGH = ON, LOW = OFF).
 *
 * If @p halt_system is true, the function does not return and the system
 * remains halted after signaling the error.
 *
 * @param code         Error code to signal.
 * @param halt_system  If true, the system will halt after signaling the error.
 */
void error_signal(ErrorCode code, bool halt_system, uint8_t blink_pin) {
    // Best-effort log (may be unavailable depending on failure path).
    LOG_ERROR("Error code %d", static_cast<int>(code));

    pinMode(blink_pin, OUTPUT);
    digitalWrite(blink_pin, LOW);  // LED OFF (active-high boards)

    const uint8_t n = blink_count(code);

    // Blink pattern: one blink per error code value.
    for (uint8_t i = 0; i < n; i++)
        {
            digitalWrite(blink_pin, HIGH);  // LED ON
            delay(kBlinkOnMs);
            digitalWrite(blink_pin, LOW);  // LED OFF
            delay(kBlinkOffMs);
        }

    delay(kSequenceGapMs);

    if (halt_system)
        {
            LOG_ERROR("Fatal error: halting system.");
            while (true)
                {
                    delay(1000);
                }
    }
}
