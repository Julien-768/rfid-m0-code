/**
 * @file SerialJsonFramer.cpp
 * @brief UART JSON framing and sanitation helpers.
 */

#include "SerialJsonFramer.h"

namespace SerialJsonFramer {

/**
 * @brief Check whether a UART line looks like a JSON object.
 *
 * @param line UART line to validate.
 *
 * @retval true  Line starts with @c { and ends with @c }.
 * @retval false Line should be ignored.
 */
bool isJsonObjectLine(const String& line) {
    return line.length() > 0 && line.startsWith("{") && line.endsWith("}");
}

/**
 * @brief Clean and extract a JSON object line from UART input.
 *
 * @param[in,out] line Raw UART line to sanitize.
 *
 * @retval true  A valid JSON object candidate was extracted.
 * @retval false No valid JSON object found.
 */
bool sanitizeJsonLine(String& line) {
    line.trim();

    String cleaned;

    for (size_t i = 0; i < line.length(); i++) {
        const char c = line[i];

        if (c >= 32 && c <= 126) {
            cleaned += c;
        }
    }

    line = cleaned;

    const int json_start = line.indexOf('{');
    const int json_end   = line.lastIndexOf('}');

    if (json_start < 0 || json_end < json_start) {
        return false;
    }

    line = line.substring(json_start, json_end + 1);

    return isJsonObjectLine(line);
}

}  // namespace SerialJsonFramer
