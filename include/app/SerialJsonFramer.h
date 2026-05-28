/**
 * @file SerialJsonFramer.h
 * @brief UART JSON framing and sanitation helpers.
 *
 * This module provides helper functions to:
 * - sanitize UART input lines
 * - remove startup noise and non-printable characters
 * - extract JSON object substrings from shared serial links
 *
 * It does not parse JSON commands or build protocol payloads.
 */

#pragma once

#include <Arduino.h>

namespace SerialJsonFramer {

/**
 * @brief Check whether a UART line looks like a JSON object.
 *
 * Used before parsing to ignore empty lines, startup noise, partial messages,
 * or non-JSON log lines received on the shared serial link.
 *
 * @param line Trimmed UART line to validate.
 *
 * @retval true  Line starts with @c { and ends with @c }.
 * @retval false Line should be ignored.
 */
bool isJsonObjectLine(const String& line);

/**
 * @brief Clean and extract a JSON object line from UART input.
 *
 * Removes non-printable characters and extracts the substring delimited
 * by the first @c { and the last @c } characters.
 *
 * This is used to tolerate UART startup noise and artifacts from the shared
 * serial link.
 *
 * @param[in,out] line Raw UART line to sanitize.
 *
 * @retval true  A valid JSON object candidate was extracted.
 * @retval false No valid JSON object found.
 */
bool sanitizeJsonLine(String& line);

}  // namespace SerialJsonFramer
