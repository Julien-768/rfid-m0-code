/**
 * @file log.cpp
 * @brief
 *
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
 * @param level One of LOG_LEVEL_ERROR, LOG_LEVEL_WARNING, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG.
 * @return A constant string such as "ERROR" or "DEBUG".
 */
static const char* logLevelTag(uint8_t level) {
    switch (level) {
        case LOG_LEVEL_ERROR:
            return "ERROR";
        case LOG_LEVEL_WARNING:
            return "WARNING";
        case LOG_LEVEL_INFO:
            return "INFO";
        case LOG_LEVEL_DEBUG:
            return "DEBUG";
        default:
            return "LOG";
    }
}

static void formatLogLine(char* out, size_t size, const char* format, const char* timestamp,
                          const char* level, const char* message, const char* source) {
    size_t pos = 0;

    for (const char* p = format; *p && pos < size - 1; ++p) {
        if (*p == '%') {
            ++p;
            const char* insert = "";

            switch (*p) {
                case 'T':
                    insert = timestamp;
                    break;
                case 'L':
                    insert = level;
                    break;
                case 'M':
                    insert = message;
                    break;
                case 'S':
                    insert = source;
                    break;
                default:
                    insert = "?";
                    break;
            }

            int written = snprintf(out + pos, size - pos, "%s", insert);
            if (written < 0) break;

            if ((size_t)written >= size - pos) {
                pos = size - 1;
                break;
            }

            pos += (size_t)written;
        } else {
            out[pos++] = *p;
        }
    }

    out[pos] = '\0';
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
 * 1. Sent to @ref log_event() for timestamping and SD card storage.
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
    char message[192];

    // Safety: ignore nullptr format string
    if (!fmt) return;

    // Format user message
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    // Build timestamp with RTC fallback
    char timestamp[32];

    if (rtc_available) {
        DateTime now                = rtc().now();
        static uint32_t last_millis = millis();
        uint32_t current_millis     = millis();
        uint16_t ms                 = current_millis % 1000;

        // Gestion du débordement (optionnel)
        if (current_millis < last_millis) {
            ms = current_millis % 1000;
        }
        last_millis = current_millis;

        snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d.%03u", now.year(),
                 now.month(), now.day(), now.hour(), now.minute(), now.second(), ms);
    } else {
        uint32_t uptime_ms = millis();
        uint32_t seconds   = uptime_ms / 1000;
        uint8_t hours      = seconds / 3600;
        uint8_t minutes    = (seconds % 3600) / 60;
        uint8_t secs       = seconds % 60;
        uint16_t ms        = uptime_ms % 1000;

        snprintf(timestamp, sizeof(timestamp), "UPTIME %02u:%02u:%02u.%03u", hours, minutes, secs,
                 ms);
    }

    // Build final formatted line
    char final[256];
    formatLogLine(final, sizeof(final), LOG_FORMAT, timestamp, logLevelTag(level), message,
                  "SYSTEM");

    // Dispatch to SD
    log_event(final);

    // Optional mirroring on Serial1
#if LOG_ENABLE_SERIAL1
    if (level >= LOG_SERIAL1_LEVEL) {
        Serial1.println(final);
    }
#endif
}

/**
 * @brief Flush pending log outputs.
 *
 * Ensures that all pending serial log data has been transmitted.
 * Useful before entering critical sections (noInterrupts, sleep, reset, etc.).
 */
void log_flush() {
#if LOG_ENABLE_SERIAL1
    Serial1.flush();
#endif
}
