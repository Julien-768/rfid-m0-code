/**
 * @file rfid_reader.h
 * @brief Header file for RFID reader utilities and definitions.
 * @author julien courtecuisse
 * @date 2026-02-24
 */

#ifndef RFID_READER_H
#define RFID_READER_H

#ifndef ARDUINO_ARCH_SAMD
#warning "This driver was validated on Feather M0 (SAMD21)"
#endif

#include <Arduino.h>
#pragma once

#include <Arduino.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @file rfid_driver.h
 * @brief Non-blocking, multi-port RFID driver with autonomous line reader, parsing, decoding and FIFO queue.
 *
 * Design goals:
 *  - No global RX state: each driver instance owns its own line reader.
 *  - Multi-port: create multiple rfid_driver_t instances (Serial1, Serial2, ...).
 *  - Non-blocking: call rfid_driver_tick() frequently from loop().
 *  - Robust parsing: CR-terminated line, overflow discard mode, sanitize + validate before decode.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Supported RFID tag types. Extend as needed. */
typedef enum : uint8_t
{
    TAG_TYPE_HDX = 0,
    TAG_TYPE_FDX,
    TAG_TYPE_EM4102,
    TAG_TYPE_OTHER
} tag_type_t;

/** A parsed/decoded tag plus a timestamp (millis). */
typedef struct
{
    char tag[32];
    uint32_t time_ms;
} tag_info_t;

/** Opaque driver instance type (defined in .cpp). */
typedef struct rfid_driver rfid_driver_t;

/**
 * @brief Initialize a driver instance.
 *
 * @param drv               Driver instance storage (must be non-null).
 * @param port              Stream to use (Serial1, Serial2, SoftwareSerial, etc).
 * @param type              Tag type (affects polling command and decoding).
 * @param poll_interval_ms  Poll interval in milliseconds (command is sent periodically).
 */
void rfid_driver_init(rfid_driver_t* drv, Stream* port, tag_type_t type, uint32_t poll_interval_ms);

/**
 * @brief Non-blocking driver tick. Call as often as possible from loop().
 *
 * Responsibilities:
 *  - Send polling command periodically (e.g., "@rq\r" for FDX).
 *  - Read up to N lines per tick (N is internal constant).
 *  - Sanitize/validate/decode lines.
 *  - Push decoded tags into FIFO queue.
 *
 * @param drv Driver instance (must be initialized).
 */
void rfid_driver_tick(rfid_driver_t* drv);

/**
 * @brief Pop one ready tag from the FIFO.
 *
 * @param drv Driver instance
 * @param out Output tag structure (must be non-null)
 * @return true if a tag was returned, false if FIFO empty
 */
bool rfid_driver_get_tag(rfid_driver_t* drv, tag_info_t* out);

/**
 * @brief Optional helper: debounce check (anti-bounce / anti-duplicate).
 *
 * Same logic as your previous code:
 *  - If current.tag equals previous.tag, accept only if dt >= delay_ms
 *  - If different tag, accept immediately
 *
 * @param previous Previous accepted tag
 * @param current  Current tag candidate
 * @param delay_ms Minimum delay between identical tags
 * @return true if should be recorded, false if should be ignored
 */
bool rfid_should_record_tag(const tag_info_t* previous, const tag_info_t* current, uint32_t delay_ms);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // RFID_READER_H
