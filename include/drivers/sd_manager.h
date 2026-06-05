/**
 * @file sd_manager.h
 * @defgroup SD_Manager SD Manager
 * @ingroup SystemModules
 * @brief SD card logging manager for the low-power data logger.
 *
 * This module provides SD card management utilities for:
 * - SD initialization
 * - Daily file creation when RTC is available
 * - Fallback file creation when RTC is unavailable
 * - Direct and buffered measurement logging
 * - Circular-buffer based SD write batching
 * - System log event writing
 *
 * ## Filename policy
 *
 * The Adalogger M0 uses FAT-compatible filenames. This module therefore keeps
 * filenames compatible with the 8.3 format.
 *
 * With RTC available:
 * - Measurements: YYYYMMDD.CSV
 * - System logs:  YYYYMMDD.LOG
 *
 * Without RTC:
 * - Measurements: NORTCnn.CSV
 * - System logs:  NORTCnn.LOG
 *
 * where `nn` is the first available index from 00 to 99.
 *
 * ## Write modes
 *
 * Direct mode:
 * - Each measurement is written immediately to the SD card.
 *
 * Buffered mode:
 * - Measurements are first stored in RAM.
 * - Data is flushed to the SD card when enough bytes are available.
 *
 * @note Buffered mode reduces SD write frequency and improves power efficiency
 *       during long deployments.
 */

#pragma once

#include <Arduino.h>
#include <RTClib.h>

/**
 * @def BUFFER_SIZE
 * @brief Size of the circular SD buffer in bytes.
 *
 * A 1024-byte buffer allows writes to be batched around SD sector boundaries.
 */
#define BUFFER_SIZE 1024

/**
 * @struct CircularBuffer
 * @brief RAM ring buffer used to batch measurement writes to SD.
 *
 * Characters are appended at @ref head and flushed from @ref tail.
 * If the buffer becomes full, the oldest data is overwritten.
 */
typedef struct {
    char buffer[BUFFER_SIZE];  ///< Raw byte storage.
    size_t head;               ///< Next write position.
    size_t tail;               ///< Next read/flush position.
} CircularBuffer;

/**
 * @brief Return the current measurement CSV filename.
 *
 * @return Pointer to a null-terminated FAT 8.3 filename.
 */
const char* get_data_filename();

/**
 * @brief Return the current system log filename.
 *
 * @return Pointer to a null-terminated FAT 8.3 filename.
 */
const char* get_log_filename();

/**
 * @brief Return the global circular buffer instance.
 *
 * @return Reference to the global @ref CircularBuffer.
 */
CircularBuffer& get_sdBuffer();

/**
 * @def sdBuffer
 * @brief Convenience macro for accessing the global SD circular buffer.
 */
#define sdBuffer get_sdBuffer()

/**
 * @brief Initialize the SD card interface.
 *
 * @param pin_cs SD card chip-select pin.
 * @return true if SD initialization succeeds, false otherwise.
 */
bool sd_initialization(uint8_t pin_cs);

/**
 * @brief Append a null-terminated string to the circular buffer.
 *
 * No newline is automatically added.
 *
 * @param cb Pointer to a valid @ref CircularBuffer.
 * @param line Null-terminated string to append.
 * @return true on success, false on invalid input.
 */
bool addToCircularBuffer(CircularBuffer* cb, const char* line);

/**
 * @brief Flush buffered measurement data to the current CSV file.
 *
 * The buffer is written only when at least one 512-byte SD sector worth of data
 * is available.
 *
 * @param cb Pointer to the circular buffer.
 * @return 0 on success or no-op, 1 on failure.
 */
u_int8_t flushCircularBuffer(CircularBuffer* cb);

/**
 * @brief Create or select the daily measurement CSV file.
 *
 * Requires a valid RTC timestamp.
 *
 * Filename format:
 * @code
 * YYYYMMDD.CSV
 * @endcode
 *
 * @param now Valid current date/time.
 * @return true on success, false on SD error.
 */
bool daily_data_file(const DateTime& now);

/**
 * @brief Create or select the daily system log file.
 *
 * Requires a valid RTC timestamp.
 *
 * Filename format:
 * @code
 * YYYYMMDD.LOG
 * @endcode
 *
 * @param now Valid current date/time.
 * @return true on success, false on SD error.
 */
bool daily_log_file(const DateTime& now);

/**
 * @brief Create or select the appropriate data and log files.
 *
 * With RTC available:
 * - Creates/selects daily files.
 * - Rolls over when the day changes.
 *
 * Without RTC:
 * - Creates/selects one no-RTC file pair for the current boot/session.
 * - Uses the first available `NORTCnn.CSV` / `NORTCnn.LOG` pair.
 *
 * @param now Current DateTime instance. Only trusted when @p rtc_available is true.
 * @param rtc_available true if RTC date/time is valid.
 * @return true on success, false on SD error.
 */
bool check_and_create_new_daily_file(const DateTime& now, bool rtc_available);

/**
 * @brief Log a single measurement.
 *
 * Output format:
 * @code
 * YYYY-MM-DD HH:MM:SS.mmm;sensor_name;value;unit;
 * @endcode
 *
 * @param now Timestamp associated with the measurement.
 * @param sensor Sensor identifier.
 * @param value Floating-point measurement value.
 * @param unit Measurement unit.
 * @param use_buffer true to use the circular buffer, false for direct SD write.
 * @return true on success, false on formatting or SD write error.
 */
bool logMeasurement(const DateTime& now,
                    const char* sensor,
                    float value,
                    const char* unit,
                    bool use_buffer = false);

/**
 * @brief Append a formatted system log event to the current LOG file.
 *
 * The message is expected to already contain timestamp and log level formatting.
 * If the LOG filename is not ready yet, the latest pending message is retained
 * and written once the file becomes available.
 *
 * @param message Null-terminated formatted log message.
 */
void log_event(const char* message);
