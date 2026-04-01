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

bool scanI2CBus();

/**
 * @brief Check and create the daily log file on SD card
 *
 * Compares the day of the month in `now` with the last logged day stored
 * in `rtc_state`. If they differ, it generates a new daily log filename
 * and updates `rtc_state.last_log_day`.
 *
 * @param now Current DateTime instance.
 */
bool check_and_create_new_daily_file(const DateTime& now);

/**
 * @brief Formats a DateTime as a filename-friendly date string.
 *
 * Produces a string in the format "YY_MM_DD", for example "24_06_30",
 * useful to generate unique daily filenames.
 *
 * @param t DateTime instance representing the current date.
 * @return String with the date formatted as "YY_MM_DD".
 */
String isoformat_date(DateTime t);

/**
 * @brief Formats a DateTime object as "YYYY-MM-DD{sep}HH:MM:SS{.ms}{sep}"
 *
 * Generates a timestamp string with optional milliseconds.
 *
 * @param t The DateTime object to format.
 * @param ms Milliseconds value to include if include_ms == true.
 * @param separator Separator string inserted between date and time, and at the end.
 * @param include_ms If true, includes milliseconds; if false, omits them.
 * @return Formatted timestamp string.
 */
String isoformat(DateTime t, int ms, const String& separator, bool include_ms = true);

// /**
//  * @brief Returns a millisecond counter synchronized with the RTC.
//  *
//  * Typically used to complement timestamps with a sub-second precision.
//  * This is a placeholder returning millis() % 1000, but can be adapted
//  * to compensate drift against the RTC.
//  *
//  * @return Milliseconds modulo 1000 as uint16_t.
//  */
// uint16_t millis_synchro(u_int16_t synchro_offset_ms, u_int16_t synchro_slope);

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
 * @brief Writes the compile date and time in ISO8601 format to a buffer.
 * (e.g., `"2025-07-23T14:30:00"`).
 *
 * @param out Output buffer for the resulting string.
 * @param len Size of the output buffer in bytes.
 */
void convertDateToISO8601(char* out, size_t len);
