#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

#define LOG_LEVEL_DEBUG 10
#define LOG_LEVEL_INFO 20
#define LOG_LEVEL_WARNING 30
#define LOG_LEVEL_ERROR 40
#define LOG_LEVEL_NONE 255

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

#ifndef LOG_ENABLE_SERIAL1
#define LOG_ENABLE_SERIAL1 0
#endif

#ifndef LOG_SERIAL1_LEVEL
#define LOG_SERIAL1_LEVEL LOG_LEVEL_DEBUG
#endif

// %T → timestamp
// %L → level (INFO, DEBUG…)
// %S → source (ex: SYSTEM)
// %M → message

#ifndef LOG_FORMAT
#define LOG_FORMAT "%T - %L - %M"
#endif

void logPrintf(uint8_t level, const char* fmt, ...);

#define LOG_ERROR(...)                                                             \
    do {                                                                           \
        if (LOG_LEVEL <= LOG_LEVEL_ERROR) logPrintf(LOG_LEVEL_ERROR, __VA_ARGS__); \
    } while (0)

#define LOG_WARN(...)                                                                  \
    do {                                                                               \
        if (LOG_LEVEL <= LOG_LEVEL_WARNING) logPrintf(LOG_LEVEL_WARNING, __VA_ARGS__); \
    } while (0)

#define LOG_INFO(...)                                                            \
    do {                                                                         \
        if (LOG_LEVEL <= LOG_LEVEL_INFO) logPrintf(LOG_LEVEL_INFO, __VA_ARGS__); \
    } while (0)

#define LOG_DEBUG(...)                                                             \
    do {                                                                           \
        if (LOG_LEVEL <= LOG_LEVEL_DEBUG) logPrintf(LOG_LEVEL_DEBUG, __VA_ARGS__); \
    } while (0)

void log_flush();
