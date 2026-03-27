/**
 * @file utils.cpp
 * @brief Utility functions for time formatting and synchronized timing.
 *
 * This file contains helper functions for:
 * - Formatting `DateTime` objects into ISO8601-like strings.
 * - Creating date-only strings for filenames.
 * - Computing millisecond timestamps synchronized with the RTC, compensating for drift.
 */

#include "utils.h"
#include "rtc.h"
#include "sd_manager.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

uint32_t synchro_offset_ms,
    previous_synchro_offset_ms;  // Offset in millisecond between the RTC and millis()
uint32_t synchro_slope = 0;      // Slope that represents the drift of the offet over the time

#include "utils.h"
#include "rtc.h"
#include "sd_manager.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void check_and_create_new_daily_file(const DateTime& now) {
    if (now.day() != rtc_state().last_log_day) {
        char file_name[16];
        strcpy(file_name, get_filename());
        daily_data_file(file_name, now);
        rtc_state().last_log_day = now.day();
    }
}

/**
 * @brief Formats a `DateTime` object as `"YYYY-MM-DD{sep}HH:MM:SS[.ms]{sep}"`.
 *
 * @param t          The `DateTime` object to format.
 * @param ms         Milliseconds value to append if `include_ms` is true.
 * @param separator  String separator inserted between date and time, and appended at the end.
 * @param include_ms If true, milliseconds are included; otherwise only full seconds are shown.
 * @return Formatted timestamp string.
 */
String isoformat(const DateTime& t, int ms, const String& separator, bool include_ms) {
    char buffer[48];  // plus grand pour inclure option ms
    if (include_ms) {
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d%s%02d:%02d:%02d.%03d%s", t.year(),
                 t.month(), t.day(), separator.c_str(), t.hour(), t.minute(), t.second(), ms,
                 separator.c_str());
    } else {
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d%s%02d:%02d:%02d%s", t.year(), t.month(),
                 t.day(), separator.c_str(), t.hour(), t.minute(), t.second(), separator.c_str());
    }
    return String(buffer);
}

/**
 * @brief Formats a `DateTime` object into a date string `"YYYY_MM_DD"`.
 *
 * Ensures leading zeros for month and day. Intended for filenames or log entries.
 *
 * @param t The `DateTime` object to format.
 * @return Formatted date string.
 */
String isoformat_date(const DateTime& t) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%04d_%02d_%02d", t.year(), t.month(), t.day());
    return String(buffer);
}

struct SynchroParams {
    u_long synchro_offset_ms;
    u_long synchro_slope;
};

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
 * @brief Convert ISO8601 datetime string to @ref LoggerTime_t (BCD-encoded).
 *
 * Expected format: `"YYYY-MM-DDTHH:MM:SS"` (e.g., `"2025-07-23T14:30:00"`).
 *
 * @param iso8601 Null-terminated ISO8601 string.
 * @param out Pointer to @ref LoggerTime_t to fill.
 * @return true on successful parsing, false otherwise.
 */
bool convertDatetoBcd(const char* iso8601, LoggerTime_t* out) {
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
 * @brief Convert a @ref LoggerTime_t (BCD-encoded) to an ISO8601 string.
 *
 * Output format: `"YYYY-MM-DDTHH:MM:SS"`.
 *
 * @param in  Pointer to BCD-encoded @ref LoggerTime_t.
 * @param out Output buffer for the ISO string.
 * @param len Length of @p out (recommend at least 20 bytes).
 */
void convertBcdDateToISO8601(const LoggerTime_t* in, char* out, size_t len) {
    snprintf(out, len, "20%02x-%02x-%02xT%02x:%02x:%02x", in->year, in->month, in->day, in->hour,
             in->minute, in->second);
}

/**
 * @brief Write the firmware compilation timestamp in ISO8601 to @p out.
 *
 * Uses the C macros `__DATE__` (e.g., "Jul 23 2025") and `__TIME__` ("HH:MM:SS"),
 * and converts them to `"YYYY-MM-DDTHH:MM:SS"`.
 *
 * @param out Output buffer.
 * @param len Size of @p out. (Recommend at least 20 bytes)
 */
void convertDateToISO8601(char* out, size_t len) {
    snprintf(out, len, "20%.*sT%.*s:00", 6, __DATE__ + 7, 5, __TIME__);
    // __DATE__ → "Jul 23 2025" ; __TIME__ → "14:30:00"
    // Convert to format like "2025-07-23T14:30:00"
}
