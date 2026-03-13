/* utils_rfid.c */
/**
 * @file utils_rfid.cpp
 * @brief RFID utility helpers (tag decoding, comparisons, and simple buzzer I/O).
 *
 * This module provides:
 *  - HEX character conversion helpers
 *  - FDX tag HEX-to-NIC decoding ("CCC-NNNNNNNNNNNN")
 *  - Utility comparison on the last digits of tag strings
 *  - A simple non-blocking buzzer/LED helper for Arduino.
 */
#include <stdio.h> /* snprintf */
#include <Arduino.h>
#include "utils_rfid.h"

/**
 * @brief Return the minimum of two uint8_t values.
 * @param a First value.
 * @param b Second value.
 * @return The smaller of @p a and @p b.
 */
static inline uint8_t u8_min(uint8_t a, uint8_t b) {
    return (a < b) ? a : b;
}

/**
 * @brief Convert a single hexadecimal character to its integer value.
 *
 * Accepts '0'..'9', 'A'..'F', and 'a'..'f'. Any other character returns 0.
 *
 * @param input Hexadecimal character.
 * @return Integer value in range [0, 15] (or 0 on invalid input).
 */
unsigned int rfid_hex2int(char input) {
    if (input >= '0' && input <= '9')
        {
            return (unsigned int)(input - '0');
    }
    if (input >= 'A' && input <= 'F')
        {
            return (unsigned int)(input - 'A' + 10);
    }
    if (input >= 'a' && input <= 'f')
        {
            return (unsigned int)(input - 'a' + 10);
    }
    return 0u;
}

/**
 * @brief Decode an FDX tag HEX string into NIC format.
 *
 * Expects an FDX payload as a fixed-length HEX string (RFID_FDX_HEX_LEN).
 * The NIC output format is: "CCC-NNNNNNNNNNNN" (3 digits + '-' + 12 digits).
 *
 * @param src Input HEX string (FDX).
 * @param out Output buffer receiving the NIC string.
 * @param out_size Size of @p out in bytes (must be >= RFID_NIC_STR_MAX).
 * @return true on successful decoding and formatting, false otherwise.
 */
bool rfid_tag_hex_to_nic(const char* src, char* out, size_t out_size) {
    if (!src || !out)
        {
            return false;
    }
    if (out_size < RFID_NIC_STR_MAX)
        {
            return false;
    }

    const size_t len = strlen(src);
    if (len != RFID_FDX_HEX_LEN)
        {
            return false;
    }

    /* Keep only the last 12 hex digits: src[4..15] inclusive */
    const char* hex12 = src + 4;

    /* Parse first 3 hex digits into z (12 bits) */
    unsigned int z = 0u;
    for (size_t i = 0u; i < 3u; i++)
        {
            const unsigned int a = rfid_hex2int(hex12[i]);
            z                    = (z << 4) | a;
        }

    /* Country code: upper 10 bits of z */
    const unsigned int country_code = (z >> 2) & 0x3FFu;

    /* NIC starts with last 2 bits of z */
    uint64_t nic = (uint64_t)(z & 0x3u);

    /* Continue parsing remaining hex digits (from index 3 to 11) */
    for (size_t i = 3u; i < 12u; i++)
        {
            const unsigned int a = rfid_hex2int(hex12[i]);
            nic                  = (nic << 4) | (uint64_t)a;
        }

    /* Output: "CCC-NNNNNNNNNNNN" => always 3 + 1 + 12 chars */
    const int written = snprintf(out, out_size, "%03u-%012llu", country_code, (unsigned long long)nic);

    return (written > 0) && ((size_t)written < out_size);
}

/**
 * @brief Compare two tag strings using up to their last 10 characters.
 *
 * The comparison starts from the end of each string and compares character-by-character.
 * It compares at most 10 characters, and at most the length of the shorter string.
 *
 * @param tag1 First tag string.
 * @param tag2 Second tag string.
 * @return true if the compared suffixes match, false otherwise.
 */
bool rfid_compare_last10(const char* tag1, const char* tag2) {
    if (!tag1 || !tag2)
        {
            return false;
    }

    const uint8_t len1 = (uint8_t)strlen(tag1);
    const uint8_t len2 = (uint8_t)strlen(tag2);

    uint8_t n = u8_min(len1, len2);
    n         = u8_min(n, 10u);

    for (uint8_t i = 0u; i < n; i++)
        {
            const char c1 = tag1[(uint8_t)(len1 - 1u - i)];
            const char c2 = tag2[(uint8_t)(len2 - 1u - i)];
            if (c1 != c2)
                {
                    return false;
            }
        }
    return true;
}

// ----------

// ---------- IO ----------

/**
 * @brief GPIO pin used for the buzzer/LED output.
 */
const int PIN_BUZZER_LED = 13;

// ---------- Buzzer non-bloquant ----------

/**
 * @brief State holder for the non-blocking buzzer/LED helper.
 */
struct BuzzerState
{
    bool active;
    uint32_t start_time;
    uint16_t duration_ms;
};

/**
 * @brief Global buzzer state instance.
 */
BuzzerState buzzer = {false, 0, 0};

/**
 * @brief Initialize the buzzer/LED GPIO.
 *
 * Configures the buzzer/LED pin as output and ensures it starts LOW.
 */
void buzzer_init() {
    pinMode(PIN_BUZZER_LED, OUTPUT);
    digitalWrite(PIN_BUZZER_LED, LOW);
}

/**
 * @brief Start a non-blocking beep for a given duration.
 *
 * Turns the buzzer/LED pin HIGH immediately and keeps it active until
 * @ref buzzer_update turns it off after the requested duration.
 *
 * @param duration_ms Beep duration in milliseconds.
 */
void buzzer_beep(uint16_t duration_ms) {
    buzzer.active      = true;
    buzzer.start_time  = millis();
    buzzer.duration_ms = duration_ms;
    digitalWrite(PIN_BUZZER_LED, HIGH);
}

/**
 * @brief Update the non-blocking buzzer/LED state.
 *
 * Call this periodically from the main loop to stop the beep once the
 * configured duration has elapsed.
 */
void buzzer_update() {
    if (!buzzer.active) return;
    if ((uint32_t)(millis() - buzzer.start_time) >= buzzer.duration_ms)
        {
            digitalWrite(PIN_BUZZER_LED, LOW);
            buzzer.active = false;
    }
}
