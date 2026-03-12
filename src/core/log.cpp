/**
 * @file log.cpp
 * @brief Centralized logging backend for the Moonraker data logger.
 *
 * This module implements the core logging pipeline:
 * - Adds a textual log level tag (ERROR, WARN, INFO, DEBUG)
 * - Formats user messages using printf-style formatting
 * - Sends the final message to logSystemEvent() for timestamped SD storage
 * - Optionally mirrors logs to Serial1, depending on compile-time settings
 *
 * User code should only call the LOG_ERROR / LOG_WARN / LOG_INFO / LOG_DEBUG
 * macros, which route into logPrintf().
 */

#include "log.h"
#include "rtc.h"
#include "sd_manager.h"

#include <SD.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>  // for strcpy, strlen

// ---------------------------------------------------------------------------
// Map numeric log level to printable tag string
// ---------------------------------------------------------------------------
/**
 * @brief Converts a numeric log level into its textual representation.
 *
 * @param level One of LOG_LEVEL_ERROR, LOG_LEVEL_WARN, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG.
 * @return A constant string such as "ERROR" or "DEBUG".
 */
static const char* logLevelTag(uint8_t level) {
    switch (level)
        {
            case LOG_LEVEL_ERROR:
                return "ERROR";
            case LOG_LEVEL_WARN:
                return "WARN";
            case LOG_LEVEL_INFO:
                return "INFO";
            case LOG_LEVEL_DEBUG:
                return "DEBUG";
            default:
                return "LOG";
        }
}

// ---------------------------------------------------------------------------
// printf-like logger backend
// ---------------------------------------------------------------------------
/**
 * @brief Formats a log message and dispatches it to the logging system.
 *
 * This function builds a single log line of the form:
 * @code
 * [LEVEL] formatted user message...
 * @endcode
 *
 * The resulting line is:
 * 1. Sent to @ref logSystemEvent() for timestamping and SD card storage.
 * 2. Optionally duplicated to Serial1 (debug output), depending on
 *    LOG_ENABLE_SERIAL1 and LOG_SERIAL1_MIN_LEVEL.
 *
 * @param level Numeric log level.
 * @param fmt   printf-style format string. Must not be nullptr.
 * @param ...   Arguments matching @p fmt.
 *
 * @note This function is normally not called directly.
 *       Use LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG.
 */
void logPrintf(uint8_t level, const char* fmt, ...) {
    char buffer[192];

    // Safety: ignore nullptr format string
    if (!fmt) return;

    // Build "[LEVEL] " prefix
    const char* tag = logLevelTag(level);
    int offset      = snprintf(buffer, sizeof(buffer), "[%s] ", tag);

    if (offset < 0 || offset >= (int)sizeof(buffer))
        {
            // Highly unlikely, fallback to a safe prefix
            strcpy(buffer, "[LOG] ");
            offset = (int)strlen(buffer);
    }

    // Format user message after prefix
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer + offset, sizeof(buffer) - offset, fmt, args);
    va_end(args);

    // Dispatch to SD + timestamp
    logSystemEvent(buffer);

    // Optional debug mirroring on Serial1
#if LOG_ENABLE_SERIAL1
    if (level <= LOG_LEVEL && level >= LOG_SERIAL1_MIN_LEVEL)
        {
            Serial1.println(buffer);
    }
#endif
}

// ---------------------------------------------------------------------------
// Final SD writer with timestamp
// ---------------------------------------------------------------------------
/**
 * @brief Writes a fully formatted log message to the SD card.
 *
 * This is the final stage of the logging pipeline. It prepends a timestamp
 * from the RTC and writes the line to the current daily log file.
 *
 * The final on-disk format is:
 * @code
 * YYYY-MM-DD HH:MM:SS;SYSTEM;<message>
 * @endcode
 *
 * If the daily file name is not yet available (very early at boot), the last
 * message is buffered in RAM and written once the filename is assigned.
 *
 * @param message User-formatted log payload (without timestamp).
 */
void logSystemEvent(const char* message) {
    static char pending[192] = {0};
    static bool hasPending   = false;

    // If log file name is not yet available, store message temporarily
    // TODO filename
    if (get_filename()[0] == '\0')
        {
            snprintf(pending, sizeof(pending), "%s", message);
            hasPending = true;
            return;
    }

    File log = SD.open(get_filename(), FILE_WRITE);
    if (!log)
        {
            // Could not write; keep last message for later flush
            snprintf(pending, sizeof(pending), "%s", message);
            hasPending = true;
            return;
    }

    // Build timestamp prefix
    DateTime now = rtc.now();
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(),
             now.second());

    // If a message was pending, flush it first
    if (hasPending)
        {
            char linePending[256];
            snprintf(linePending, sizeof(linePending), "%s;SYSTEM;%s", timestamp, pending);
            log.println(linePending);

            hasPending = false;
            pending[0] = '\0';
    }

    // Write current message
    char line[256];
    snprintf(line, sizeof(line), "%s;SYSTEM;%s", timestamp, message);
    log.println(line);

    log.close();
}
