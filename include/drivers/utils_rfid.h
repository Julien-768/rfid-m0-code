/**
 * @file utils_rfid.h
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

/* utils_rfid.h */
#ifndef UTILS_RFID_H
#define UTILS_RFID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Maximum length (including '\0') of an FDX-B NIC string: "250-228500042234" => 17 bytes */
#define RFID_NIC_STR_MAX 17u

/** Expected length (without '\0') of an FDX tag expressed as hexadecimal ASCII characters (e.g., "80003EB533A9F1FA") */
#define RFID_FDX_HEX_LEN 16u

/**
 * @brief Convert one hexadecimal character to its integer value.
 *
 * Accepts '0'..'9', 'A'..'F', 'a'..'f'.
 *
 * @param input Hexadecimal ASCII character.
 * @return Value in range [0..15] if valid, otherwise 0.
 */
unsigned int rfid_hex2int(char input);

/**
 * @brief Convert an FDX tag from hexadecimal ASCII to NIC string "CCC-NNNNNNNNNNNN".
 *
 * Expected input is a 16-hex-char ASCII string (FDX tag): e.g. "80003EB533A9F1FA".
 * The function takes the last 12 hex chars (bits containing country code + national ID),
 * extracts:
 *   - country code: 10 bits (formatted as 3 decimal digits, zero-padded)
 *   - NIC: 38 bits (formatted as 12 decimal digits, zero-padded)
 *
 * Output format: "%03u-%012llu"
 *
 * @param src Null-terminated input string (length must be exactly RFID_FDX_HEX_LEN).
 * @param out Output buffer where the NIC string is written.
 * @param out_size Size of output buffer in bytes (must be >= RFID_NIC_STR_MAX).
 * @return true on success, false on invalid input or insufficient output buffer.
 */
bool rfid_tag_hex_to_nic(const char* src, char* out, size_t out_size);

/**
 * @brief Compare only the last up-to-10 characters of two tag strings.
 *
 * The function compares from the end of each string, up to the minimum length
 * of the two strings, but no more than 10 characters.
 *
 * @param tag1 Null-terminated string.
 * @param tag2 Null-terminated string.
 * @return true if the compared suffixes match, false otherwise.
 */
bool rfid_compare_last10(const char* tag1, const char* tag2);

#endif /* UTILS_RFID_H */
