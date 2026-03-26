/**
 * @file sd_manager.h
 * @brief SD card logging manager for the Moonraker low-power data logger.
 *
 * This module provides all SD card management utilities, including:
 * - SD initialization
 * - Daily file creation
 * - Direct and buffered write modes
 * - Structured measurement logging using @ref logMeasurement()
 * - High-level logging of multi-sensor frames via @ref logSensorFrame()
 *
 * The SD Manager operates in two write modes:
 * - **Direct mode** (immediate `File.write()`)
 * - **Buffered mode** using a RAM-based circular buffer to batch writes
 *
 * @note Buffered mode reduces the number of SD writes and improves
 *       power efficiency during long deployments.
 *
 * Example (DEPLOY mode):
 * @code
 * if (!sd_initialization()) { return; }
 *
 * DateTime now = rtc().now();
 * daily_data_file(get_filename(), now);
 *
 * SensorFrame frame = readAllSensors();
 * logSensorFrame(now, frame);   // writes multiple lines (one per measurement)
 *
 * flushCircularBuffer(&sdBuffer);  // periodically
 * @endcode
 *
 * @ingroup SD_Manager
 */

#pragma once

#include <RTClib.h>  ///< For DateTime support

const char* get_filename();  // Déclaration de la fonction

struct SensorFrame;  // Forward declaration

/**
 * @def BUFFER_SIZE
 * @brief Size of the circular buffer in bytes (default: 1024).
 *
 * Using a 1024-byte ring buffer provides efficient batching aligned with
 * SD card block sizes (512 bytes), reducing the number of write cycles.
 */
#define BUFFER_SIZE 1024

/**
 * @struct CircularBuffer
 * @brief Ring-buffer structure for batching SD writes.
 *
 * Characters are appended at @ref head and flushed from @ref tail.
 * When full, the buffer automatically overwrites the oldest data
 * and logs a warning (first occurrence only).
 */
typedef struct {
    char buffer[BUFFER_SIZE];  ///< Raw byte storage.
    size_t head;               ///< Next write position.
    size_t tail;               ///< Next read/flush position.
} CircularBuffer;

/**
 * @brief Returns a reference to the global circular buffer for SD writes.
 */
extern CircularBuffer& get_sdBuffer();

// Optional: Macro for convenience (use with caution)
#define sdBuffer get_sdBuffer()

/**
 * @brief Get the current log filename used for SD writes.
 *
 * @return Pointer to a null-terminated C-string with the filename.
 */
extern const char* get_filename();

/**
 * @brief Initialize the SD card interface.
 *
 * Mounts the SD card using the hardware SPI peripheral.
 * On success, a system-level log entry is emitted.
 * On failure, the system transitions to END-OF-LIFE mode.
 *
 * @return `true` if the SD card was successfully initialized.
 */
bool sd_initialization(uint8_t pin_cs);

/**
 * @brief Append a null-terminated text line to the circular buffer.
 *
 * Each character is pushed into the ring buffer. If the buffer becomes full,
 * the oldest data is overwritten to guarantee continuous logging.
 *
 * @param cb   Pointer to a valid @ref CircularBuffer.
 * @param line C-string to add (without automatic newline).
 */
void addToCircularBuffer(CircularBuffer* cb, const char* line);

/**
 * @brief Flush the circular buffer contents to the SD card.
 *
 * Data is written only when at least 512 bytes are buffered
 * (matching the SD sector size), improving write efficiency.
 *
 * @param cb Pointer to the @ref CircularBuffer instance.
 */
u_int8_t flushCircularBuffer(CircularBuffer* cb);

/**
 * @brief Create or open the daily log file based on the given timestamp.
 *
 * Generates a filename in `YYYYMMDD.TXT` format and ensures the file exists.
 * If the file cannot be created or opened, the logger transitions to
 * END-OF-LIFE mode.
 *
 * @param filename Output buffer where the filename (8.3 format) is stored.
 * @param now      Current timestamp used to generate the filename.
 */
u_int8_t daily_data_file(char* filename, const DateTime& now);

/**
 * @brief Log a single measurement in semicolon-delimited format.
 *
 * Output format:
 * @code
 * YYYY-MM-DD HH:MM:SS;sensor_name;value;unit;
 * @endcode
 *
 * @param now    Timestamp associated with the measurement.
 * @param sensor Sensor identifier (e.g. `"AS7341_F1_415nm"`).
 * @param value  Floating-point measurement value.
 * @param unit   Unit string (e.g. `"count"`, `"lux"`, `"V"`).
 */
u_int8_t logMeasurement(const DateTime& now, const char* sensor, float value, const char* unit,
                        bool use_buffer = false);
