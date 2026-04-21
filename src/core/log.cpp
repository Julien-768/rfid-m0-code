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
#include <string.h>
#include <wiring_private.h>  // SERCOM

// ---------------------------------------------------------------------------
// Alternate UART on D12 (RX) / D6 (TX)
// ---------------------------------------------------------------------------
#if (LOG_SERIAL_OUTPUT == LOG_ALT_SERIAL)

Uart SerialAlt(&sercom2, 12, 6, SERCOM_RX_PAD_1, UART_TX_PAD_0);

void SERCOM2_Handler() {
    SerialAlt.IrqHandler();
}

#endif

// ---------------------------------------------------------------------------
// Map numeric log level to printable tag string
// ---------------------------------------------------------------------------
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
                case 'L': {
                    const int width = 7;  // largeur fixe
                    int written     = snprintf(out + pos, size - pos, "%-*s", width, level);
                    if (written < 0) break;

                    if ((size_t)written >= size - pos) {
                        pos = size - 1;
                        break;
                    }

                    pos += (size_t)written;
                    continue;
                }
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
void logPrintf(uint8_t level, const char* fmt, ...) {
    char message[192];

    if (!fmt) return;

    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    char timestamp[32];

    if (rtc_available) {
        DateTime now                = rtc().now();
        static uint32_t last_millis = millis();
        uint32_t current_millis     = millis();
        uint16_t ms                 = current_millis % 1000;

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

    char final[256];
    formatLogLine(final, sizeof(final), LOG_FORMAT, timestamp, logLevelTag(level), message,
                  "SYSTEM");

    log_event(final);

#if (LOG_SERIAL_OUTPUT == LOG_SERIAL1)
    if (level >= LOG_SERIAL_LEVEL) {
        Serial1.println(final);
    }
#endif

    // #if (LOG_SERIAL_OUTPUT == LOG_ALT_SERIAL)
    //     if (level >= LOG_SERIAL_LEVEL) {
    //         SerialAlt.println(final);
    //     }
    // #endif
}

void log_flush() {
#if (LOG_SERIAL_OUTPUT == LOG_SERIAL1)
    Serial1.flush();
#endif
    // #if (LOG_SERIAL_OUTPUT == LOG_ALT_SERIAL)
    //     SerialAlt.flush();
    // #endif
}
