/**
 * @file utils.h
 * @brief Utility functions for time formatting and RTC synchronization.
 *
 * This module provides helpers to format DateTime objects as strings
 * (e.g., for filenames or log entries) and to calculate a synchronized
 * millisecond timestamp for accurate time logs.
 */

#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include "rtc.h"

struct IsoFormatOptions {
    int ms                = 0;
    const char* separator = "T";
    bool include_ms       = false;
};

bool scanI2CBus();

/**
 * @brief Formats a DateTime object as "YYYY-MM-DD{sep}HH:MM:SS[.mmm]".
 *
 * @param t DateTime object to format.
 * @param opts Formatting options: separator and optional milliseconds.
 * @return Formatted timestamp string.
 */
String isoformat(const DateTime& t, const IsoFormatOptions& opts);

/**
 * @brief Converts a BCD-encoded byte to its decimal value.
 *
 * @param val BCD-encoded value (e.g., 0x25 for decimal 25)
 * @return Decimal integer equivalent of the BCD value.
 */
uint8_t bcdToDec(uint8_t val);

/**
 * @brief Parses an ISO8601 datetime string and stores it in BCD format.
 *
 * Converts an ISO8601 string of the form `"YYYY-MM-DDTHH:MM:SS"` into a
 * `LoggerTime_t` struct where each field is stored in BCD format.
 *
 * @param iso8601 Input string, e.g. `"2025-07-23T14:30:00"`.
 * @param out Pointer to a LoggerTime_t struct to receive the BCD-encoded result.
 * @return `true` if parsing and conversion succeeded, `false` otherwise.
 */
bool convertDatetoBcd(const char* iso8601, LoggerTime_t* out);

/**
 * @brief Formats a BCD-encoded LoggerTime_t as an ISO8601 string.
 *
 * Converts a `LoggerTime_t` struct (with BCD-encoded fields) into a
 * standard ISO8601 timestamp string.
 *
 * @param in Pointer to the LoggerTime_t to convert.
 * @param out Output buffer for the resulting string.
 * @param len Size of the output buffer in bytes (should be ≥ 20).
 */
void convertBcdDateToISO8601(const LoggerTime_t* in, char* out, size_t len);

/**
 * @brief Convert an ISO8601 datetime string to a DateTime object.
 *
 * Expected format:
 * `"YYYY-MM-DDTHH:MM:SS"`
 *
 * Example:
 * `"2025-07-23T14:30:00"`
 *
 * @param iso8601 Null-terminated ISO8601 string.
 * @param out Pointer to the destination DateTime object.
 * @return true if parsing succeeded, false otherwise.
 */
bool convertISO8601ToDateTime(const char* iso8601, DateTime* out);

/**
 * @brief Formats a DateTime as a filename-friendly date string.
 *
 * Produces a string in the format "YYYY_MM_DD", for example "2026_06_05".
 *
 * @param t DateTime instance to format.
 * @return String with the date formatted as "YYYY_MM_DD".
 */
String isoformat_date(const DateTime& t);

/**
 * @brief Format a date/time as ISO8601.
 *
 * Output format: "YYYY-MM-DDTHH:MM:SS".
 *
 * @param out Output buffer.
 * @param len Output buffer size, minimum 20 bytes.
 * @param year Year.
 * @param month Month [1..12].
 * @param day Day [1..31].
 * @param hour Hour [0..23].
 * @param minute Minute [0..59].
 * @param second Second [0..59].
 * @return true if formatting succeeded, false otherwise.
 */
bool formatISO8601(char* out,
                   size_t len,
                   uint16_t year,
                   uint8_t month,
                   uint8_t day,
                   uint8_t hour,
                   uint8_t minute,
                   uint8_t second);
