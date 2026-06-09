/**
 * @file utils.h
 * @brief Utility functions for date/time formatting, conversion and hardware helpers.
 */

#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include "rtc.h"

/**
 * @brief Options used by isoformat() to control timestamp formatting.
 */
struct IsoFormatOptions {
    /// Milliseconds value to append when include_ms is true.
    int ms = 0;

    /// Separator inserted between date and time.
    const char* separator = "T";

    /// If true, append milliseconds as ".mmm".
    bool include_ms = false;
};

/**
 * @brief Scan the I2C bus and report detected devices.
 *
 * @return true if at least one I2C device was found, false otherwise.
 */
bool scanI2CBus();

/**
 * @brief Formats a DateTime object as "YYYY-MM-DD{sep}HH:MM:SS[.ms]".
 *
 * @param t The DateTime object to format.
 * @param opts Formatting options.
 * @return Formatted timestamp string.
 */
String isoformat(const DateTime& t, const IsoFormatOptions& opts = IsoFormatOptions());

/**
 * @brief Formats a DateTime object as "YYYY_MM_DD".
 *
 * @param t The DateTime object to format.
 * @return Formatted date string.
 */
String isoformat_date(const DateTime& t);

/**
 * @brief Converts a BCD-encoded byte to decimal.
 *
 * @param val BCD-encoded value.
 * @return Decimal value.
 */
uint8_t bcdToDec(uint8_t val);

/**
 * @brief Parses an ISO8601 datetime string and stores it in BCD format.
 *
 * @param iso8601 Input string, e.g. "2025-07-23T14:30:00".
 * @param out Pointer to the LoggerTime_t output structure.
 * @return true if parsing succeeded, false otherwise.
 */
bool convertDatetoBcd(const char* iso8601, deviceTime_t* out);

/**
 * @brief Formats a BCD-encoded deviceTime_t as an ISO8601 string.
 *
 * @param in Pointer to the BCD-encoded LoggerTime_t.
 * @param out Output buffer.
 * @param len Output buffer size in bytes.
 */
void convertBcdDateToISO8601(const LoggerTime_t* in, char* out, size_t len);

/**
 * @brief Converts an ISO8601 datetime string to a DateTime object.
 *
 * @param iso8601 Input ISO8601 string.
 * @param out Pointer to the destination DateTime object.
 * @return true if parsing succeeded, false otherwise.
 */
bool convertISO8601ToDateTime(const char* iso8601, DateTime* out);

/**
 * @brief Returns the current logger timestamp.
 *
 * @return Current date and time.
 */
DateTime logger_now();

/**
 * @brief Returns the current millisecond fraction of the logger timestamp.
 *
 * @return Milliseconds in the range [0, 999].
 */
uint16_t logger_ms();
