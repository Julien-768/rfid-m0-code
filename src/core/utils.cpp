/**
 * @file utils.cpp
 * @brief Utility functions for time formatting, BCD conversion and synchronized timing.
 *
 * This file contains helper functions for:
 * - Formatting `DateTime` objects into ISO8601-like strings.
 * - Creating date-only strings for filenames.
 * - Converting ISO8601 strings to device/RTC date structures.
 * - Computing millisecond timestamps synchronized with the RTC.
 */

#include "utils.h"

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "rtc.h"
#include "sd_manager.h"

uint32_t synchro_offset_ms,
    previous_synchro_offset_ms;  // Offset in millisecond between the RTC and millis()
uint32_t synchro_slope = 0;      // Slope that represents the drift of the offset over time

/**
 * @brief Scan the I2C bus and log detected devices.
 *
 * @return true if at least one I2C device responds, false otherwise.
 */
bool scanI2CBus() {
    uint8_t count = 0;

    LOG_DEBUG("Scanning I2C bus...");

    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t error = Wire.endTransmission();

        if (error == 0) {
            LOG_DEBUG("I2C device found at 0x%02X", addr);
            count++;
        } else if (error == 4) {
            LOG_DEBUG("Unknown error at 0x%02X", addr);
        }
    }

    if (count == 0) {
        LOG_DEBUG("No I2C devices found");
        return false;
    } else {
        LOG_DEBUG("Scan complete. %d device(s) found.", count);
        return true;
    }
}

/**
 * @brief Formats a DateTime object as `"YYYY-MM-DD{sep}HH:MM:SS[.mmm]"`.
 *
 * @param t    DateTime object to format.
 * @param opts Formatting options: separator and optional milliseconds.
 * @return Formatted timestamp string.
 */
String isoformat(const DateTime& t, const IsoFormatOptions& opts) {
    char buffer[48];

    if (opts.include_ms) {
        snprintf(buffer,
                 sizeof(buffer),
                 "%04d-%02d-%02d%s%02d:%02d:%02d.%03d",
                 t.year(),
                 t.month(),
                 t.day(),
                 opts.separator,
                 t.hour(),
                 t.minute(),
                 t.second(),
                 opts.ms);
    } else {
        snprintf(buffer,
                 sizeof(buffer),
                 "%04d-%02d-%02d%s%02d:%02d:%02d",
                 t.year(),
                 t.month(),
                 t.day(),
                 opts.separator,
                 t.hour(),
                 t.minute(),
                 t.second());
    }

    return String(buffer);
}

/**
 * @brief Formats a DateTime object into a date string.
 *
 * Output format: `"YYYY_MM_DD"`.
 * Intended for filenames and daily log names.
 *
 * @param t DateTime object to format.
 * @return Formatted date string.
 */
String isoformat_date(const DateTime& t) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%04d_%02d_%02d", t.year(), t.month(), t.day());
    return String(buffer);
}

// struct SynchroParams {
//     u_long synchro_offset_ms;
//     u_long synchro_slope;
// };

// /**
//  *  @brief Synchronizes the system clock with the
//  * RTC (Real Time Clock) using the `millis
//  * `synchro_offset_ms` and `synchro_slope
//  * `parameters.

//  * @return The corrected `millis` value relative to
//  * the RTC.
//  */
// uint16_t millis_synchro(const SynchroParams& params) {

//     int32_t delta_ms = (int32_t)millis() - synchro_offset_ms;

//     // Estimate the drift since the last RTC sync
//     int32_t synchro_drift_est_ms = synchro_slope * delta_ms;
//     synchro_drift_est_ms /= 1000;

//     // Compute corrected millis relative to RTC
//     int32_t millisSynchro_ms = delta_ms - synchro_drift_est_ms;

//     // Keep only the fractional millisecond part
//     millisSynchro_ms = millisSynchro_ms % 1000;

//     return (uint16_t)millisSynchro_ms;
// }

/**
 * @brief Convert an unsigned integer (0–99) to BCD.
 * @param val Value to convert (0..99)
 * @return BCD-encoded byte.
 */
static uint8_t toBCD(int val) {
    return ((val / 10) << 4) | (val % 10);
}

/**
 * @brief Convert a BCD-encoded byte to decimal (0..99).
 * @param val BCD-encoded byte.
 * @return Decimal value.
 */
uint8_t bcdToDec(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

/**
 * @brief Convert ISO8601 datetime string to @ref deviceTime_t (BCD-encoded).
 *
 * Expected format: `"YYYY-MM-DDTHH:MM:SS"` (e.g., `"2025-07-23T14:30:00"`).
 *
 * @param iso8601 Null-terminated ISO8601 string.
 * @param out Pointer to @ref deviceTime_t to fill.
 * @return true on successful parsing, false otherwise.
 */
bool convertDatetoBcd(const char* iso8601, deviceTime_t* out) {
    int y, m, d, h, min, s;
    if (sscanf(iso8601, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &min, &s) != 6) return false;

    out->year   = toBCD(y % 100);
    out->month  = toBCD(m);
    out->day    = toBCD(d);
    out->hour   = toBCD(h);
    out->minute = toBCD(min);
    out->second = toBCD(s);

    return true;
}

/**
 * @brief Convert a @ref deviceTime_t (BCD-encoded) to an ISO8601 string.
 *
 * Output format: `"YYYY-MM-DDTHH:MM:SS"`.
 *
 * @param in  Pointer to BCD-encoded @ref deviceTime_t.
 * @param out Output buffer for the ISO string.
 * @param len Length of @p out (recommend at least 20 bytes).
 */
void convertBcdDateToISO8601(const deviceTime_t* in, char* out, size_t len) {
    snprintf(out,
             len,
             "20%02x-%02x-%02xT%02x:%02x:%02x",
             in->year,
             in->month,
             in->day,
             in->hour,
             in->minute,
             in->second);
}

/**
 * @brief Format a date/time as ISO8601.
 *
 * Example:
 * 2025-07-23T14:30:00
 *
 * @param out Output buffer.
 * @param len Output buffer size (minimum 20 bytes).
 * @param year Year.
 * @param month Month [1..12].
 * @param day Day [1..31].
 * @param hour Hour [0..23].
 * @param minute Minute [0..59].
 * @param second Second [0..59].
 *
 * @return true if formatting succeeded.
 * @return false if arguments are invalid.
 */
bool formatISO8601(char* out,
                   size_t len,
                   uint16_t year,
                   uint8_t month,
                   uint8_t day,
                   uint8_t hour,
                   uint8_t minute,
                   uint8_t second) {
    if (!out || len < 20) {
        return false;
    }

    if (month < 1 || month > 12) {
        return false;
    }

    if (day < 1 || day > 31) {
        return false;
    }

    snprintf(out, len, "%04u-%02u-%02uT%02u:%02u:%02u", year, month, day, hour, minute, second);

    return true;
}

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
bool convertISO8601ToDateTime(const char* iso8601, DateTime* out) {
    if (!iso8601 || !out) {
        return false;
    }

    deviceTime_t t{};

    if (!convertDatetoBcd(iso8601, &t)) {
        return false;
    }

    *out = DateTime(2000 + bcdToDec(t.year),
                    bcdToDec(t.month),
                    bcdToDec(t.day),
                    bcdToDec(t.hour),
                    bcdToDec(t.minute),
                    bcdToDec(t.second));

    return true;
}
