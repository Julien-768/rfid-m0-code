/**
 * @file log.h
 * @brief Public logging API for the Moonraker system.
 *
 * This module provides a unified printf-style logging interface with
 * configurable log levels and optional Serial1 mirroring.
 *
 * The high-level macros (LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG)
 * format messages through logPrintf(), which prepends a tag such as
 * "[INFO]" and sends the message into the centralized backend
 * implemented in log.cpp.
 *
 * Typical output format on the SD card:
 * @code
 * 2025-01-30 12:34:56;SYSTEM;[INFO] Sensor initialized
 * @endcode
 */

#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Log level definitions
// ---------------------------------------------------------------------------
/**
 * @name Log level values
 * @brief Compile-time numeric values used to filter log output.
 * @{
 */
#define LOG_LEVEL_NONE 0   ///< Logging disabled
#define LOG_LEVEL_ERROR 1  ///< Only errors are logged
#define LOG_LEVEL_WARN 2   ///< Errors + warnings
#define LOG_LEVEL_INFO 3   ///< Errors + warnings + info
#define LOG_LEVEL_DEBUG 4  ///< Full verbose logging
/** @} */

/**
 * @brief Default compile-time log level.
 *
 * Can be overridden in platformio.ini using:
 * @code
 * -DLOG_LEVEL=LOG_LEVEL_INFO
 * @endcode
 */
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

// ---------------------------------------------------------------------------
// Output options (compile-time)
// ---------------------------------------------------------------------------

/**
 * @brief Enable or disable mirroring of logs to Serial1.
 *
 * @value 0 Log to SD only
 * @value 1 Log to SD + Serial1
 *
 * Example override:
 * @code
 * -DLOG_ENABLE_SERIAL1=1
 * @endcode
 */
#ifndef LOG_ENABLE_SERIAL1
#define LOG_ENABLE_SERIAL1 0
#endif

/**
 * @brief Minimum log level required to output messages on Serial1.
 *
 * Example override:
 * @code
 * -DLOG_SERIAL1_MIN_LEVEL=LOG_LEVEL_INFO
 * @endcode
 */
#ifndef LOG_SERIAL1_MIN_LEVEL
#define LOG_SERIAL1_MIN_LEVEL LOG_LEVEL_DEBUG
#endif

// ---------------------------------------------------------------------------
// Backend functions (implemented in log.cpp)
// ---------------------------------------------------------------------------

/**
 * @brief Final SD-card writer.
 *
 * Receives an already-formatted message and adds a timestamp before writing
 * it to the current daily log file.
 *
 * @param message Null-terminated string containing the log payload.
 */
void logSystemEvent(const char* message);

/**
 * @brief printf-style log formatter used internally by all LOG_* macros.
 *
 * @param level Numeric log level (LOG_LEVEL_ERROR, LOG_LEVEL_WARN, ...).
 * @param fmt   printf-style format string.
 * @param ...   Additional arguments matching @p fmt.
 *
 * @note Applications should not call this directly.
 *       Use LOG_ERROR(), LOG_WARN(), LOG_INFO(), LOG_DEBUG().
 */
void logPrintf(uint8_t level, const char* fmt, ...);

// ---------------------------------------------------------------------------
// Public logging macros
// ---------------------------------------------------------------------------

/**
 * @brief Log an error-level message.
 *
 * Usage:
 * @code
 * LOG_ERROR("Failed to read sensor: %d", errorCode);
 * @endcode
 */
#define LOG_ERROR(...)                                                             \
    do {                                                                           \
        if (LOG_LEVEL >= LOG_LEVEL_ERROR) logPrintf(LOG_LEVEL_ERROR, __VA_ARGS__); \
    } while (0)

/**
 * @brief Log a warning-level message.
 */
#define LOG_WARN(...)                                                            \
    do {                                                                         \
        if (LOG_LEVEL >= LOG_LEVEL_WARN) logPrintf(LOG_LEVEL_WARN, __VA_ARGS__); \
    } while (0)

/**
 * @brief Log an informational message.
 */
#define LOG_INFO(...)                                                            \
    do {                                                                         \
        if (LOG_LEVEL >= LOG_LEVEL_INFO) logPrintf(LOG_LEVEL_INFO, __VA_ARGS__); \
    } while (0)

/**
 * @brief Log a debug-level message.
 *
 * Debug logs are stripped automatically at compile time
 * when LOG_LEVEL is below LOG_LEVEL_DEBUG.
 */
#define LOG_DEBUG(...)                                                             \
    do {                                                                           \
        if (LOG_LEVEL >= LOG_LEVEL_DEBUG) logPrintf(LOG_LEVEL_DEBUG, __VA_ARGS__); \
    } while (0)
