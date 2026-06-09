/**
 * @file log.cpp
 * @brief device backend for serial and SD output.
 */

/**
 * @section platform_info Platform Information
 * - Platform: Adafruit Feather M0 (ATSAMD21G18)
 * - MCU: ARM Cortex-M0+ @ 48 MHz
 * - Framework: Arduino (SAMD core)
 * - Logic Level: 3.3V
 */

#include "log.h"
#include "rtc.h"
#include "sd_manager.h"
#include "hardware.h"
#include "SoftTx.h"
#include "utils.h"

#include <SD.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if (LOG_SERIAL_OUTPUT == LOG_ALT_SERIAL)
SoftTx SerialAlt(SerialAlt_TX, 9600);
#endif

// ---------------------------------------------------------------------------
// Serial device initialization
// ---------------------------------------------------------------------------
void logInit() {
#if (LOG_SERIAL_OUTPUT == LOG_USB_SERIAL)
    Serial.begin(115200);

    uint32_t start = millis();
    while (!Serial && (millis() - start < 3000)) {}

    Serial.println("USB Serial OK");

#elif (LOG_SERIAL_OUTPUT == LOG_SERIAL1)
    Serial1.begin(115200);
    delay(100);
    Serial1.println("Serial1 OK");

#elif (LOG_SERIAL_OUTPUT == LOG_ALT_SERIAL)
    SerialAlt.begin();
    SerialAlt.print("Soft TX on D");
    SerialAlt.print(SerialAlt_TX);
    SerialAlt.println(" OK");
#endif
}

// ---------------------------------------------------------------------------
// Log level helpers
// Convention assumed:
// ERROR   = most important
// WARNING
// INFO
// DEBUG   = least important
//
// Therefore: a message is emitted if level >= configured threshold.
// ---------------------------------------------------------------------------
static bool logLevelEnabled(uint8_t level, uint8_t threshold) {
    return level >= threshold;
}

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

// ---------------------------------------------------------------------------
// Safe append helper
// ---------------------------------------------------------------------------
static void appendText(char* out, size_t size, size_t* pos, const char* text) {
    if (!out || !pos || size == 0) return;
    if (!text) text = "";

    while (*text && *pos < size - 1) {
        out[(*pos)++] = *text++;
    }

    out[*pos] = '\0';
}

static void appendPaddedLevel(char* out, size_t size, size_t* pos, const char* level) {
    if (!level) level = "";

    const uint8_t width = 7;
    uint8_t len         = strlen(level);

    appendText(out, size, pos, level);

    while (len < width && *pos < size - 1) {
        out[(*pos)++] = ' ';
        len++;
    }

    out[*pos] = '\0';
}

// ---------------------------------------------------------------------------
// Format log line
// Supported placeholders:
// %T = timestamp
// %L = padded log level
// %M = message
// %S = source
// %% = literal percent
// ---------------------------------------------------------------------------
static void formatLogLine(char* out,
                          size_t size,
                          const char* format,
                          const char* timestamp,
                          const char* level,
                          const char* message,
                          const char* source) {
    if (!out || size == 0) return;

    out[0] = '\0';

    if (!format) {
        format = "%T [%L] %S: %M";
    }

    if (!timestamp) timestamp = "";
    if (!level) level = "";
    if (!message) message = "";
    if (!source) source = "";

    size_t pos = 0;

    for (const char* p = format; *p && pos < size - 1; ++p) {
        if (*p != '%') {
            out[pos++] = *p;
            out[pos]   = '\0';
            continue;
        }

        ++p;

        if (*p == '\0') {
            appendText(out, size, &pos, "%");
            break;
        }

        switch (*p) {
            case 'T':
                appendText(out, size, &pos, timestamp);
                break;

            case 'L':
                appendPaddedLevel(out, size, &pos, level);
                break;

            case 'M':
                appendText(out, size, &pos, message);
                break;

            case 'S':
                appendText(out, size, &pos, source);
                break;

            case '%':
                appendText(out, size, &pos, "%");
                break;

            default:
                appendText(out, size, &pos, "?");
                break;
        }
    }

    out[pos] = '\0';
}

// ---------------------------------------------------------------------------
// Timestamp formatting
// ---------------------------------------------------------------------------
static void formatTimestamp(char* timestamp, size_t size) {
    if (!timestamp || size == 0) return;

    DateTime now = device_now();

    IsoFormatOptions opts;
    opts.separator  = " ";
    opts.include_ms = true;
    opts.ms         = device_ms();

    String formatted = isoformat(now, opts);

    snprintf(timestamp, size, "%s", formatted.c_str());
}

// ---------------------------------------------------------------------------
// printf-like device backend
// ---------------------------------------------------------------------------
void logPrintf(uint8_t level, const char* fmt, ...) {
    if (!fmt) return;

    // Early exit if neither SD nor serial needs this level.
#if defined(LOG_SD_LEVEL)
    const bool sd_enabled = logLevelEnabled(level, LOG_SD_LEVEL);
#else
    const bool sd_enabled = true;
#endif

    const bool serial_enabled = logLevelEnabled(level, LOG_SERIAL_LEVEL);

    if (!sd_enabled && !serial_enabled) {
        return;
    }

    char message[192];

    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    char timestamp[40];
    formatTimestamp(timestamp, sizeof(timestamp));

    // If RTC is not available, prepend "UPTIME" to the timestamp to indicate it's not real time.
    if (!rtc_available) {
        char tmp[48];
        snprintf(tmp, sizeof(tmp), "UPTIME %s", timestamp);

        strncpy(timestamp, tmp, sizeof(timestamp) - 1);
        timestamp[sizeof(timestamp) - 1] = '\0';
    }

    char final[256];
    formatLogLine(final,
                  sizeof(final),
                  LOG_FORMAT,
                  timestamp,
                  logLevelTag(level),
                  message,
                  "SYSTEM");

#if defined(LOG_SD_LEVEL)
    if (sd_enabled) {
        log_event(final);
    }
#else
    log_event(final);
#endif

#if (LOG_SERIAL_OUTPUT == LOG_USB_SERIAL)
    if (serial_enabled) {
        Serial.println(final);
    }
#elif (LOG_SERIAL_OUTPUT == LOG_SERIAL1)
    if (serial_enabled) {
        Serial1.println(final);
    }
#elif (LOG_SERIAL_OUTPUT == LOG_ALT_SERIAL)
    if (serial_enabled) {
        SerialAlt.println(final);
    }
#endif
}

// ---------------------------------------------------------------------------
// Flush device outputs
// ---------------------------------------------------------------------------
void log_flush() {
#if (LOG_SERIAL_OUTPUT == LOG_USB_SERIAL)
    Serial.flush();
#elif (LOG_SERIAL_OUTPUT == LOG_SERIAL1)
    Serial1.flush();
#elif (LOG_SERIAL_OUTPUT == LOG_ALT_SERIAL)
    SerialAlt.flush();
#endif
}
