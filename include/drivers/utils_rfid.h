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
