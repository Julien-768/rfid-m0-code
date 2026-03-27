/**
 * @file sd_manager.cpp
 * @defgroup SD_Manager SD Manager
 * @ingroup SystemModules
 * @brief SD card management and buffered data logging for the Moonraker Logger.
 *
 * The **SD Manager** module handles initialization, daily file management, and
 * buffered writing for the Moonraker low-power data logger. It ensures
 * reliable, power-efficient logging even under intermittent storage access
 * or during long deployments.
 *
 * ## Responsibilities
 * - Initialize and verify SD card communication.
 * - Manage a circular buffer for batched SD writes (reduces power usage).
 * - Create and maintain daily log files with unique filenames.
 * - Write measurement data from sensors and system telemetry.
 * - Handle SD write failures and transition to safe shutdown (END-OF-LIFE).
 *
 * ## Error Handling
 * - On initialization failure → @ref ERR_SD_NOT_FOUND, enters `STATE_ENDOFLIFE`.
 * - On write or flush error → @ref ERR_SD_WRITE_FAIL, enters `STATE_ENDOFLIFE`.
 *
 * @endcode
 * @{
 */

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "sd_manager.h"
#include "log.h"
#include "error_handler.h"
#include "hardware.h"

// Define the global filename buffer for daily log file
char filename_data[16] = {0};  // e.g., "20251124.TXT"

// Implement the getter function
const char* get_filename() {
    return filename_data;
}

// Define the global CircularBuffer object
CircularBuffer sdBuffer_instance;  // Default constructor

// Implement the getter function
CircularBuffer& get_sdBuffer() {
    return sdBuffer_instance;
}

/**
 * @brief Initialize the SD card interface.
 *
 * Uses the chip select pin (@ref PIN_SD_CS) to mount the SD card via SPI.
 * On success, logs an info message. On failure, signals an LED error pattern
 * and requests transition to @ref STATE_ENDOFLIFE.
 *
 * @return `true` if SD initialized successfully, `false` if not detected.
 *
 * @note This function must be called once during @ref STATE_BOOT.
 * @see error_signal(), logSystemEvent(), ERR_SD_NOT_FOUND
 */
bool sd_initialization(uint8_t pin_cs) {

    if (!SD.begin(pin_cs)) {
        // 🔴 Blink code 2× — non-blocking
        error_signal(ERR_SD_NOT_FOUND);

        return false;  // ❌ Initialization failed
    }
    LOG_INFO("SD card initialized successfully");
    return true;  // ✅ OK
}

/**
 * @brief Add a string line to the circular RAM buffer for delayed SD write.
 *
 * This function appends a null-terminated string to a circular buffer in RAM.
 * If the buffer becomes full, the oldest data is overwritten (tail advanced).
 *
 * @param cb   Pointer to a @ref CircularBuffer instance.
 * @param line Null-terminated C-string to store (no newline added automatically).
 *
 * @note A warning is logged on first overflow to avoid log spamming.
 */
void addToCircularBuffer(CircularBuffer* cb, const char* line) {
    static bool overflowWarned = false;
    size_t len                 = strlen(line);

    for (size_t i = 0; i < len; i++) {
        cb->buffer[cb->head] = line[i];
        cb->head             = (cb->head + 1) % BUFFER_SIZE;

        if (cb->head == cb->tail) {
            if (!overflowWarned) {
                LOG_WARN("Buffer overflow: advancing tail to avoid overwrite");
                overflowWarned = true;
            }
            cb->tail = (cb->tail + 1) % BUFFER_SIZE;
        }
    }
}

/**
 * @brief Flush buffered data to the SD card when ≥512 bytes are available.
 *
 * Writes data from the circular buffer to the SD card in 512-byte blocks
 * (aligned with SD sector size). If a write or open operation fails,
 * the system logs the error, signals the appropriate error code,
 * and transitions to @ref STATE_ENDOFLIFE.
 *
 * @param cb Pointer to the @ref CircularBuffer to flush.
 *
 * @see error_signal(), logSystemEvent(), daily_data_file()
 */
u_int8_t flushCircularBuffer(CircularBuffer* cb) {
    size_t buffered =
        (cb->head >= cb->tail) ? (cb->head - cb->tail) : (BUFFER_SIZE - cb->tail + cb->head);

    if (buffered < 512) return 0;  // Nothing to flush yet

    File log = SD.open(get_filename(), FILE_WRITE);
    if (!log) {
        error_signal(ERR_SD_WRITE_FAIL);
        return -1;
    }

    bool writeSuccess = true;

    if (cb->head > cb->tail) {
        size_t bytesWritten = log.write((uint8_t*)&cb->buffer[cb->tail], cb->head - cb->tail);
        if (bytesWritten != (cb->head - cb->tail)) writeSuccess = false;
    } else {
        size_t part1         = BUFFER_SIZE - cb->tail;
        size_t bytesWritten1 = log.write((uint8_t*)&cb->buffer[cb->tail], part1);
        size_t bytesWritten2 = log.write((uint8_t*)&cb->buffer[0], cb->head);
        if (bytesWritten1 != part1 || bytesWritten2 != cb->head) writeSuccess = false;
    }

    log.close();

    if (!writeSuccess) {
        error_signal(ERR_SD_WRITE_FAIL);
        return -1;
    } else {
        cb->tail = cb->head;  // Reset buffer after successful flush
        LOG_INFO("Circular buffer flushed to SD successfully");
    }
    return 0;
}

/**
 * @brief Create or open the daily data file for logging.
 *
 * Generates a filename using the pattern `YYYYMMDD.TXT`.
 * If the file cannot be created or opened, signals @ref ERR_SD_WRITE_FAIL
 * and transitions to @ref STATE_ENDOFLIFE.
 *
 * @param filename Output buffer (char[13]) for the generated filename.
 * @param now      Current date/time used for naming.
 *
 * @see rtc().now(), error()
 */
bool daily_data_file(char* filename, const DateTime& now) {
    snprintf(filename, 16, "%04d%02d%02d.TXT", now.year(), now.month(), now.day());

    File logfile = SD.open(filename, FILE_WRITE);
    if (!logfile) {
        error_signal(ERR_SD_WRITE_FAIL);
        return false;
    }

    logfile.close();

    LOG_INFO("Daily log file ready: %s", filename);
    return true;
}

/**
 * @brief Log a single timestamped measurement to SD or buffer.
 *
 * Output format:
 * @code
 * YYYY-MM-DD HH:MM:SS;sensor_name;value;unit;
 * @endcode
 *
 * Writing mode depends on @ref config.use_buffer:
 * - If `true` → line is appended to the circular buffer.
 * - If `false` → line is written directly to the current daily file.
 *
 * On any write failure, the function logs the error and transitions
 * the system to @ref STATE_ENDOFLIFE.
 *
 * @param now    Current timestamp (RTC).
 * @param sensor Sensor identifier string.
 * @param value  Measured value.
 * @param unit   Measurement unit (e.g. `"lux"`, `"count"`, `"V"`).
 */
u_int8_t logMeasurement(const DateTime& now, const char* sensor, float value, const char* unit,
                        bool use_buffer) {
    char timestamp[25];
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(),
             now.day(), now.hour(), now.minute(), now.second());

    String line = String(timestamp) + ";" + sensor + ";" + String(value, 3) + ";" + unit + ";";

    if (use_buffer) {
        addToCircularBuffer(&sdBuffer, line.c_str());
        return 0;
    }

    File log = SD.open(get_filename(), FILE_WRITE);
    if (!log) {
        error_signal(ERR_SD_WRITE_FAIL);
        return -1;
    }

    size_t written = log.println(line);
    log.close();

    if (written == 0) {
        error_signal(ERR_SD_WRITE_FAIL);
        return -1;
    }
    return 0;
}

/** @} */  // end of SD_Manager group
