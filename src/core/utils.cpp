/**
 * @file utils.cpp
 * @brief Utility functions for date/time formatting, conversion and hardware helpers.
 */

#include "utils.h"

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "rtc.h"
#include "sd_manager.h"
#include "log.h"

/**
 * @brief Offset between RTC time and the Arduino millis() counter.
 */
uint32_t synchro_offset_ms = 0;

/**
 * @brief Previous RTC synchronization offset.
 */
uint32_t previous_synchro_offset_ms = 0;

/**
 * @brief Estimated drift slope of the millis() counter relative to the RTC.
 */
uint32_t synchro_slope = 0;

/**
 * @brief Scan the I2C bus and report detected devices.
 *
 * Iterates through valid I2C addresses and checks for responding devices.
 *
 * @return true if at least one I2C device was found, false otherwise.
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
    }

    LOG_DEBUG("Scan complete. %d device(s) found.", count);
    return true;
}

/**
 * @brief Formats a DateTime object as "YYYY-MM-DD{sep}HH:MM:SS[.ms]".
 *
 * @param t The DateTime object to format.
 * @param opts Formatting options.
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
 * @brief Formats a DateTime object as "YYYY_MM_DD".
 *
 * @param t The DateTime object to format.
 * @return Formatted date string.
 */
String isoformat_date(const DateTime& t) {
    char buffer[16];

    snprintf(buffer, sizeof(buffer), "%04d_%02d_%02d", t.year(), t.month(), t.day());

    return String(buffer);
}

/**
 * @brief Converts a decimal value to BCD.
 *
 * Encodes an integer in the range [0, 99] into a BCD byte.
 *
 * @param val Decimal value to convert.
 * @return BCD-encoded byte.
 */
static uint8_t toBCD(int val) {
    return ((val / 10) << 4) | (val % 10);
}

/**
 * @brief Converts a BCD-encoded byte to decimal.
 *
 * @param val BCD-encoded value.
 * @return Decimal value.
 */
uint8_t bcdToDec(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

/**
 * @brief Parses an ISO8601 datetime string and stores it in BCD format.
 *
 * Expected format: "YYYY-MM-DDTHH:MM:SS".
 *
 * @param iso8601 Input string.
 * @param out Pointer to the LoggerTime_t output structure.
 * @return true if parsing succeeded, false otherwise.
 */
bool convertDatetoBcd(const char* iso8601, LoggerTime_t* out) {
    if (!iso8601 || !out) {
        return false;
    }

    int y, m, d, h, min, s;

    if (sscanf(iso8601, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &min, &s) != 6) {
        return false;
    }

    out->year   = toBCD(y % 100);
    out->month  = toBCD(m);
    out->day    = toBCD(d);
    out->hour   = toBCD(h);
    out->minute = toBCD(min);
    out->second = toBCD(s);

    return true;
}

/**
 * @brief Formats a BCD-encoded LoggerTime_t as an ISO8601 string.
 *
 * Output format: "YYYY-MM-DDTHH:MM:SS".
 *
 * @param in Pointer to the BCD-encoded LoggerTime_t.
 * @param out Output buffer.
 * @param len Output buffer size in bytes.
 */
void convertBcdDateToISO8601(const LoggerTime_t* in, char* out, size_t len) {
    if (!in || !out || len == 0) {
        return;
    }

    snprintf(out,
             len,
             "20%02u-%02u-%02uT%02u:%02u:%02u",
             bcdToDec(in->year),
             bcdToDec(in->month),
             bcdToDec(in->day),
             bcdToDec(in->hour),
             bcdToDec(in->minute),
             bcdToDec(in->second));
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

/**
 * @brief Returns the current logger timestamp.
 *
 * Uses the RTC when available. If the RTC is not available, falls back to
 * the firmware build date and advances it using millis().
 *
 * @return Current date and time.
 */
DateTime logger_now() {
    if (rtc_available) {
        return rtc().now();
    }

    static const DateTime fallback_start(__DATE__, __TIME__);
    return fallback_start + TimeSpan(millis() / 1000UL);
}

/**
 * @brief Returns the current millisecond fraction of the logger timestamp.
 *
 * @return Milliseconds in the range [0, 999].
 */
uint16_t logger_ms() {
    return millis() % 1000UL;
}
