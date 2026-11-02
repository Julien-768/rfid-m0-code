/**
 * @file SerialJsonFramer.cpp
 * @brief UART JSON framing and sanitation helpers.
 *
 * @version 3.0.0
 * @date 2022-08-17
 *
 * @section authors Authors
 * - Julien Courtecuisse
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: julien.courtecuisse@iphc.cnrs.fr
 *
 * @section project Project
 * Name: RFID-M0
 * Description: Embedded firmware for the open-source RFID-M0 project, designed to monitor small animal species around burrows and nest boxes. The software integrates RFID-based individual identification, infrared passage detection, real-time clock (RTC) timestamping, and SD-card data logging, with low-power management for autonomous field deployments. It supports ecological research, including long-term monitoring of Great Tits (Parus major) and research on the European hamster (Cricetus cricetus) conducted within the cross-border CRICETUS project.
 *
 * @section contributors Contributors
 * - Robin Laesser
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: robin.laesser@iphc.cnrs.fr
 *
 * @section license License
 * Copyright (C) 2022 Julien Courtecuisse
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * License: GPL-3.0-or-later
 * https://www.gnu.org/licenses/gpl-3.0.html
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
