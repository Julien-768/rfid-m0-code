/**
 * @file error_handler.h
 * @brief Error management system using the built-in LED.
 *
 * This module provides a minimal error signaling mechanism based on
 * LED blink patterns. It is intended for use in critical failure paths
 * where serial output or logging may not be available.
 *
 * Typical use cases:
 * - early boot failures (SD, RTC)
 * - critical runtime conditions (battery critical)
 *
 * @ingroup SystemModules
 */

#pragma once

#ifndef
#define PIN_ERROR PIN_LEDBUILTIN
#endif

/**
 * @enum ErrorCode
 * @brief Error code definitions for LED-based signaling.
 *
 * Each error code corresponds to a specific failure condition and may
 * be represented by a distinct LED blink pattern.
 *
 * The exact blink encoding is implementation-defined
 * (see error_handler.cpp).
 *
 * @ingroup SystemModules
 */
enum ErrorCode : uint8_t
{
    ERR_NONE             = 0,  ///< No error
    ERR_SD_NOT_FOUND     = 2,  ///< SD card not detected
    ERR_SD_WRITE_FAIL    = 3,  ///< Failed to write on SD card (possible corruption)
    ERR_RTC_FAILURE      = 4,  ///< RTC not detected or battery depleted
    ERR_BATTERY_LOW      = 5,  ///< Battery voltage low but not critical
    ERR_BATTERY_CRITICAL = 6,  ///< Battery voltage critically low, shutdown required
    ERR_I2C_NOT_READY    = 7   ///< I2C bus not ready (Wire not initialized or busy)
};

/**
 * @brief Signal an error using LED blink patterns.
 *
 * @details
 * Encodes the provided @ref ErrorCode as a LED blink pattern.
 * Depending on @p halt_system, the function may block indefinitely
 * after signaling the error.
 *
 * This function is designed to be safe to call when:
 * - Serial output is unavailable
 * - SD card logging is unavailable
 * - The system is in a degraded or fatal state
 *
 * @param code        Error code to signal.
 * @param halt_system If true, the system halts indefinitely after signaling.
 *
 * @ingroup SystemModules
 */
void error_signal(ErrorCode code, bool halt_system, uint8_t blink_pin = PIN_ERROR);
