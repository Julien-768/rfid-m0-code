/**
 * @file rfid_driver.cpp
 * @brief Robust non-blocking RFID driver implementation for SAMD21.
 *
 * This module implements a non-blocking RFID driver supporting FDX, HDX,
 * and EM4102 tag types. It features:
 *  - Periodic polling of the reader
 *  - Explicit immediate polling on demand
 *  - Robust CR-terminated line parsing with overflow protection
 *  - Input sanitization and HEX validation
 *  - Optional FDX decoding to NIC format
 *  - Internal FIFO queue for tag buffering
 *
 * Designed for Arduino framework (SAMD core, 3.3V logic).
 */

/**
 * @section platform_info Platform Information
 * - Platform: Adafruit Feather M0 (ATSAMD21G18)
 * - MCU: ARM Cortex-M0+ @ 48 MHz
 * - Framework: Arduino (SAMD core)
 * - Logic Level: 3.3V
 */

#ifndef ARDUINO_ARCH_SAMD
#warning "This driver was validated on Feather M0 (SAMD21)"
#endif

#include "rfid_driver.h"

#include <ctype.h>
#include <string.h>
#include "log.h"

#include "utils_rfid.h"  // provides: rfid_tag_hex_to_nic(...), RFID_FDX_HEX_LEN, etc.

/* ------------------------ Small safe string helpers ------------------------ */

/**
 * @brief Safely copy a string into a fixed-size buffer.
 *
 * Always guarantees NUL-termination if dst_sz > 0.
 *
 * @param dst Destination buffer.
 * @param dst_sz Size of destination buffer.
 * @param src Source string, may be null.
 */
static void safe_strcpy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
}

/* ------------------------ Line reader (autonomous) ------------------------ */

/**
 * @brief Initialize a line reader instance.
 *
 * @param lr Pointer to line reader structure.
 */
static void line_reader_init(line_reader_t* lr) {
    lr->len        = 0;
    lr->discarding = false;
    lr->line_ready = false;
    lr->buf[0]     = '\0';
}

/**
 * @brief Poll a stream and accumulate characters until a CR-terminated line is complete.
 *
 * Non-blocking. Returns true if a full line is ready.
 *
 * @param lr Line reader instance.
 * @param s Arduino Stream source.
 * @return true if a complete line is available.
 */
static bool line_reader_poll(line_reader_t* lr, Stream* s) {
    if (lr->line_ready) {
        return true;
    }

    while (s->available()) {
        char c = (char)s->read();

        if (lr->discarding) {
            if (c == '\r') {
                lr->discarding = false;
                lr->len        = 0;
            }
            continue;
        }

        // Ignore LF
        if (c == '\n') {
            continue;
        }

        // End of line
        if (c == '\r') {
            lr->buf[lr->len] = '\0';
            lr->line_ready   = true;
            lr->len          = 0;
            return true;
        }

        // Store character if buffer has room
        if (lr->len < sizeof(lr->buf) - 1) {
            lr->buf[lr->len++] = c;
        } else {
            // Overflow protection
            lr->discarding = true;
            lr->len        = 0;
            LOG_WARN("RFID line buffer overflow, discarding line");
        }
    }

    return false;
}

/**
 * @brief Retrieve the last completed line.
 *
 * Copies the internal buffer into the provided output buffer.
 *
 * @param lr Line reader instance.
 * @param out Destination buffer.
 * @param out_sz Size of destination buffer.
 * @return true if a line was successfully retrieved.
 */
static bool line_reader_get(line_reader_t* lr, char* out, size_t out_sz) {
    if (!lr->line_ready || !out || out_sz == 0) return false;
    safe_strcpy(out, out_sz, lr->buf);
    lr->line_ready = false;
    return true;
}

/* ------------------------ Sanitizers / validators ------------------------ */

/**
 * @brief Trim leading and trailing spaces and tabs in-place.
 *
 * @param s String to trim.
 */
static void trim_inplace(char* s) {
    if (!s) return;

    char* p = s;
    while (*p == ' ' || *p == '\t') p++;

    if (p != s) memmove(s, p, strlen(p) + 1);

    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[n - 1] = '\0';
        n--;
    }
}

/**
 * @brief Sanitize a raw RFID line into a cleaned HEX string.
 *
 * Rules:
 *  - Remove occurrences of "+ "
 *  - Remove leading "rq" or "ru" prefix (case-insensitive)
 *
 * @param src Source string.
 * @param dst Destination buffer.
 * @param dst_sz Destination buffer size.
 * @return true on success.
 */
static bool sanitize_tag(const char* src, char* dst, size_t dst_sz) {
    if (!src || !dst || dst_sz == 0) return false;

    const char* r    = src;
    char* w          = dst;
    size_t remaining = dst_sz - 1;

    while (*r && remaining) {
        if (*r == '+' && r[1] == ' ') {
            r += 2;
            continue;
        }
        *w++ = *r++;
        remaining--;
    }
    *w = '\0';

    size_t len = strlen(dst);
    if (len >= 2) {
        char a = (char)tolower((unsigned char)dst[0]);
        char b = (char)tolower((unsigned char)dst[1]);
        if (a == 'r' && (b == 'q' || b == 'u')) {
            memmove(dst, dst + 2, len - 2 + 1);
        }
    }
    trim_inplace(dst);

    if (dst[0] == '>') {
        memmove(dst, dst + 1, strlen(dst));
        trim_inplace(dst);
    }
    return true;
}

/**
 * @brief Check whether a character is a valid hexadecimal digit.
 *
 * @param c Character to test.
 * @return true if valid hexadecimal digit.
 */
static bool is_hex_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

typedef struct {
    size_t min_len;
    size_t exact_len;
} HexLengthConstraints;

/**
 * @brief Validate a string as hexadecimal with optional length constraints.
 *
 * @param s Input string.
 * @param constraints Length constraints.
 * @return true if valid.
 */
static bool is_valid_hex(const char* s, HexLengthConstraints constraints) {
    if (!s) return false;

    size_t len = 0;
    while (*s) {
        if (!is_hex_char(*s++)) return false;
        len++;
    }

    if (len < constraints.min_len) return false;
    if (constraints.exact_len != 0 && len != constraints.exact_len) return false;
    return true;
}

/* ------------------------ Driver helpers ------------------------ */

/**
 * @brief Get the poll command string for a given tag type.
 *
 * @param t Tag type.
 * @return Command string to send to the reader, or null if unsupported.
 */
static const char* cmd_for(tag_type_t t) {
    switch (t) {
        case TAG_TYPE_FDX:
            return "@rq\r";
        case TAG_TYPE_HDX:
            return "@todo\r";
        case TAG_TYPE_EM4102:
            return "@ru\r";
        default:
            return NULL;
    }
}

/**
 * @brief Push a tag into the internal FIFO queue.
 *
 * If the queue is full, the oldest entry is dropped.
 *
 * @param d Driver instance.
 * @param t Tag info to push.
 */
static void queue_push(rfid_driver_t* d, const tag_info_t* t) {
    if (d->count == rfid_driver_t::QSIZE) {
        d->head = (uint8_t)((d->head + 1) % rfid_driver_t::QSIZE);
        d->count--;
    }
    d->q[d->tail] = *t;
    d->tail       = (uint8_t)((d->tail + 1) % rfid_driver_t::QSIZE);
    d->count++;
}

/**
 * @brief Pop a tag from the internal FIFO queue.
 *
 * @param d Driver instance.
 * @param out Output tag structure.
 * @return true if a tag was available.
 */
static bool queue_pop(rfid_driver_t* d, tag_info_t* out) {
    if (d->count == 0) return false;
    *out    = d->q[d->head];
    d->head = (uint8_t)((d->head + 1) % rfid_driver_t::QSIZE);
    d->count--;
    return true;
}

/* ------------------------ Public API ------------------------ */

/**
 * @brief Initialize an RFID driver instance.
 *
 * @param drv Driver instance.
 * @param port Serial stream.
 * @param type Tag type.
 * @param poll_interval_ms Polling interval in milliseconds.
 */
void rfid_driver::init(rfid_driver_t* drv,
                       Stream* port,
                       tag_type_t type,
                       uint32_t poll_interval_ms) {
    if (!drv) return;

    drv->port = port;
    drv->type = type;

    drv->poll_interval_ms = poll_interval_ms;
    drv->last_poll        = millis();

    line_reader_init(&drv->lr);

    drv->head  = 0;
    drv->tail  = 0;
    drv->count = 0;
}

/**
 * @brief Start the RFID driver on a hardware serial port.
 *
 * This function takes ownership of the provided UART for RFID communication.
 * It is intended to be called when entering DEPLOY mode, after the GUI layer
 * has released Serial1 ownership.
 *
 * Startup sequence:
 *  - Stop the UART to reset any previous runtime state.
 *  - Restart the UART at the RFID reader baudrate.
 *  - Wait for the RFID reader UART interface to stabilize.
 *  - Flush any stale or incomplete bytes from the RX buffer.
 *  - Initialize the RFID driver instance.
 *
 * The RX flush step is important because the RFID reader may emit startup
 * bytes, partial frames, or line noise immediately after power-up or UART
 * reassignment.
 *
 * This function does not power the RFID reader itself. Power management must
 * be handled externally by the deployment state machine.
 *
 * @param drv               Driver instance storage.
 * @param serial            Hardware serial port connected to the RFID reader.
 * @param type              RFID tag type.
 * @param poll_interval_ms  Poll interval in milliseconds.
 */
void rfid_driver::start(rfid_driver_t* drv,
                        HardwareSerial* serial,
                        tag_type_t type,
                        uint32_t poll_interval_ms) {
    if (!drv || !serial) return;

    serial->end();
    delay(20);

    serial->begin(9600);
    delay(100);

    drv->port = serial;
    flush_rx(drv);

    init(drv, serial, type, poll_interval_ms);

    LOG_INFO("RFID driver started on hardware serial port");
}

/**
 * @brief Stop the RFID driver and release its serial port.
 *
 * Flushes any pending TX data, stops the UART peripheral, and detaches the
 * serial stream from the driver instance.
 *
 * This allows the UART to be safely reused by another runtime mode, such as
 * the GUI communication layer during CONNECTED mode.
 *
 * This function does not disable RFID reader power. Power management remains
 * the responsibility of the deployment state machine.
 *
 * @param drv Driver instance.
 */
void rfid_driver::stop(rfid_driver_t* drv) {
    if (!drv || !drv->port) return;

    HardwareSerial* serial = static_cast<HardwareSerial*>(drv->port);

    serial->flush();
    serial->end();

    drv->port = nullptr;
}

/**
 * @brief Clear all pending bytes from the RFID receive stream.
 *
 * Removes all currently available bytes from the UART RX buffer.
 *
 * This is primarily used:
 *  - After RFID reader startup.
 *  - After UART ownership reassignment.
 *  - Before beginning a new RFID polling session.
 *
 * The goal is to discard stale, partial, or noisy data that could otherwise
 * corrupt line parsing or RFID frame decoding.
 *
 * This function does not modify the decoded tag FIFO, line reader state, or
 * polling timers.
 *
 * @param drv Driver instance.
 */
void rfid_driver::flush_rx(rfid_driver_t* drv) {
    if (!drv || !drv->port) return;

    while (drv->port->available()) {
        drv->port->read();
    }
}

/**
 * @brief Send a poll command immediately.
 *
 * This function writes directly to the configured serial stream and does not
 * wait for the periodic polling interval.
 *
 * @param drv Driver instance.
 */
void rfid_driver::poll_now(rfid_driver_t* drv) {
    if (!drv || !drv->port) return;

    const char* cmd = cmd_for(drv->type);
    if (!cmd) return;

    drv->port->print(cmd);

    LOG_DEBUG("type=%d, sent immediate poll command: %s", drv->type, cmd);
}

/**
 * @brief Non-blocking RFID driver update function.
 *
 * This function implements the full RFID acquisition pipeline and must be
 * called regularly from the main loop. It does NOT block and processes only
 * a limited amount of work per call.
 *
 * Behavior:
 *  1. Periodically sends a poll command to the RFID reader based on
 *     poll_interval_ms.
 *  2. Reads incoming serial data character-by-character from the reader.
 *  3. Reconstructs CR-terminated lines using an internal line reader.
 *  4. Sanitizes each line (removes protocol artifacts such as "+ ", "rq", "ru").
 *  5. Validates the cleaned data as a proper hexadecimal tag.
 *  6. Decodes the tag if required (e.g., FDX to NIC format).
 *  7. Pushes valid tags into an internal FIFO queue.
 *
 * Notes:
 *  - The function is non-blocking: it only processes available data and returns immediately.
 *  - A small fixed number of lines are processed per call to ensure real-time behavior.
 *  - Tag retrieval must be done separately using rfid_driver::get_tag().
 *  - This function acts as a background processing engine and does not return tags directly.
 *
 * @param drv Driver instance (must be initialized).
 */
void rfid_driver::tick(rfid_driver_t* drv) {
    if (!drv || !drv->port) return;

    const uint32_t now = millis();

    if ((uint32_t)(now - drv->last_poll) >= drv->poll_interval_ms) {
        drv->last_poll = now;

        const char* cmd = cmd_for(drv->type);
        if (cmd) {
            drv->port->print(cmd);
            LOG_DEBUG("RFID type=%d, poll command sent: %s", drv->type, cmd);
        }
    }

    uint8_t max_lines = 2;

    while (max_lines-- && line_reader_poll(&drv->lr, drv->port)) {
        char raw[64];
        char cleaned[64];
        char decoded[32];

        if (!line_reader_get(&drv->lr, raw, sizeof(raw))) {
            break;
        }

        trim_inplace(raw);

        if (!sanitize_tag(raw, cleaned, sizeof(cleaned))) {
            continue;
        }

        trim_inplace(cleaned);

        if (strcmp(cleaned, "- 1") == 0) {
            LOG_DEBUG("RFID reader response: no tag/read failed");
            continue;
        }

        if (drv->type == TAG_TYPE_FDX) {
            if (!is_valid_hex(cleaned, {RFID_FDX_HEX_LEN, RFID_FDX_HEX_LEN})) {
                LOG_WARN("RFID invalid FDX response: '%s'", cleaned);
                continue;
            }

            if (!rfid_tag_hex_to_nic(cleaned, decoded, sizeof(decoded))) {
                LOG_WARN("RFID FDX decode failed: '%s'", cleaned);
                continue;
            }
        } else {
            if (!is_valid_hex(cleaned, {5, 0})) {
                LOG_WARN("RFID invalid tag response: '%s'", cleaned);
                continue;
            }

            safe_strcpy(decoded, sizeof(decoded), cleaned);
        }

        tag_info_t ti;
        safe_strcpy(ti.tag, sizeof(ti.tag), decoded);
        ti.time_ms = millis();

        queue_push(drv, &ti);
    }
}

/**
 * @brief Retrieve the next available tag from the driver queue.
 *
 * @param drv Driver instance.
 * @param out Output tag structure.
 * @return true if a tag was available.
 */
bool rfid_driver::get_tag(rfid_driver_t* drv, tag_info_t* out) {
    if (!drv || !out) return false;
    return queue_pop(drv, out);
}

/**
 * @brief Determine whether a tag should be recorded based on debounce delay.
 *
 * Prevents repeated recording of the same tag within a given time window.
 *
 * @param previous Previously recorded tag.
 * @param current Currently read tag.
 * @param delay_ms Minimum delay between identical tags.
 * @return true if the tag should be recorded.
 */
bool rfid_driver::should_record_tag(const tag_info_t* previous,
                                    const tag_info_t* current,
                                    uint32_t delay_ms) {
    if (!previous || !current) return false;

    if (strcmp(previous->tag, current->tag) == 0) {
        const uint32_t dt = (uint32_t)(current->time_ms - previous->time_ms);
        return dt >= delay_ms;
    }
    return true;
}
