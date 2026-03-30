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
#include <string.h>    // for strcpy, strlen
#include "assembly.h"  // for hw_assembly.rtc_type

// TODO remove dependency on rtc
// TODO remove dependency on hw_assembly

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
        DateTime now = rtc().now();
        snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d", now.year(),
                 now.month(), now.day(), now.hour(), now.minute(), now.second());
    } else {
        snprintf(timestamp, sizeof(timestamp), "NO_RTC");
    }

    // Build final formatted line
    char final[256];
    formatLogLine(final, sizeof(final), LOG_FORMAT, timestamp, logLevelTag(level), message,
                  "SYSTEM");

    // Dispatch to SD
    logSystemEvent(final);

    // Optional mirroring on Serial1
#if LOG_ENABLE_SERIAL1
    if (level >= LOG_SERIAL1_LEVEL) {
        Serial1.println(final);
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
    static char pending[256] = {0};
    static bool hasPending   = false;

    if (get_filename()[0] == '\0') {
        snprintf(pending, sizeof(pending), "%s", message);
        hasPending = true;
        return;
    }

    File log = SD.open(get_filename(), FILE_WRITE);
    if (!log) {
        snprintf(pending, sizeof(pending), "%s", message);
        hasPending = true;
        return;
    }

    if (hasPending) {
        log.println(pending);
        hasPending = false;
        pending[0] = '\0';
    }

    log.println(message);
    log.close();
}
