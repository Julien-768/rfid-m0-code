#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Log levels
// Higher value = higher severity
// ---------------------------------------------------------------------------
#define LOG_LEVEL_DEBUG 10
#define LOG_LEVEL_INFO 20
#define LOG_LEVEL_WARNING 30
#define LOG_LEVEL_ERROR 40
#define LOG_LEVEL_NONE 255

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

// Optional SD logging threshold.
// Messages with level >= LOG_SD_LEVEL are written to SD.
#ifndef LOG_SD_LEVEL
#define LOG_SD_LEVEL LOG_LEVEL_DEBUG
#endif

// ---------------------------------------------------------------------------
// Serial output configuration
// ---------------------------------------------------------------------------
// Available modes:
//   LOG_SERIAL_NONE : no serial output
//   LOG_USB_SERIAL  : output to USB Serial
//   LOG_SERIAL1     : output to Serial1
//   LOG_ALT_SERIAL  : output to SerialAlt / SoftTx

#define LOG_SERIAL_NONE 0
#define LOG_USB_SERIAL 1
#define LOG_SERIAL1 2
#define LOG_ALT_SERIAL 3

#ifndef LOG_SERIAL_OUTPUT
#define LOG_SERIAL_OUTPUT LOG_USB_SERIAL
#endif

// Messages with level >= LOG_SERIAL_LEVEL are printed to serial.
#ifndef LOG_SERIAL_LEVEL
#define LOG_SERIAL_LEVEL LOG_LEVEL_DEBUG
#endif

// ---------------------------------------------------------------------------
// Log format
// ---------------------------------------------------------------------------
// Supported placeholders:
//   %T : timestamp
//   %L : level, padded to fixed width
//   %S : source
//   %M : message
//   %% : literal percent

#ifndef LOG_FORMAT
#define LOG_FORMAT "%T - %L - %M"
#endif

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void logInit();
void logPrintf(uint8_t level, const char* fmt, ...);
void log_flush();

// ---------------------------------------------------------------------------
// Convenience macros
// ---------------------------------------------------------------------------
#define LOG_ERROR(...)                               \
    do {                                             \
        if (LOG_LEVEL <= LOG_LEVEL_ERROR) {          \
            logPrintf(LOG_LEVEL_ERROR, __VA_ARGS__); \
        }                                            \
    } while (0)

#define LOG_WARN(...)                                  \
    do {                                               \
        if (LOG_LEVEL <= LOG_LEVEL_WARNING) {          \
            logPrintf(LOG_LEVEL_WARNING, __VA_ARGS__); \
        }                                              \
    } while (0)

#define LOG_INFO(...)                               \
    do {                                            \
        if (LOG_LEVEL <= LOG_LEVEL_INFO) {          \
            logPrintf(LOG_LEVEL_INFO, __VA_ARGS__); \
        }                                           \
    } while (0)

#define LOG_DEBUG(...)                               \
    do {                                             \
        if (LOG_LEVEL <= LOG_LEVEL_DEBUG) {          \
            logPrintf(LOG_LEVEL_DEBUG, __VA_ARGS__); \
        }                                            \
    } while (0)
