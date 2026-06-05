/**
 * @file sd_manager.cpp
 * @defgroup SD_Manager SD Manager
 * @ingroup SystemModules
 * @brief SD card management and buffered data logging for the Logger.
 */

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>

#include "sd_manager.h"
#include "log.h"
#include "error_handler.h"
#include "hardware.h"
#include "utils.h"

// ---------------------------------------------------------------------------
// Global filenames
// ---------------------------------------------------------------------------

/**
 * @brief Current measurement CSV filename.
 *
 * FAT 8.3 compatible.
 * Examples:
 * - With RTC:    20260205.CSV
 * - Without RTC: NORTC00.CSV
 */
char filename_data[16] = {0};

/**
 * @brief Current system log filename.
 *
 * FAT 8.3 compatible.
 * Examples:
 * - With RTC:    20260205.LOG
 * - Without RTC: NORTC00.LOG
 */
char filename_log[16] = {0};

/**
 * @brief Return the current measurement CSV filename.
 *
 * @return Null-terminated filename string.
 */
const char* get_data_filename() {
    return filename_data;
}

/**
 * @brief Return the current system log filename.
 *
 * @return Null-terminated filename string.
 */
const char* get_log_filename() {
    return filename_log;
}

/**
 * @brief Global SD circular buffer instance.
 */
CircularBuffer sdBuffer_instance;

/**
 * @brief Return the global SD circular buffer instance.
 *
 * @return Reference to the circular buffer.
 */
CircularBuffer& get_sdBuffer() {
    return sdBuffer_instance;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/**
 * @brief Build a daily FAT 8.3 filename from a valid RTC date.
 *
 * Format: YYYYMMDD.EXT
 *
 * @param out Destination buffer.
 * @param size Destination buffer size.
 * @param now Current RTC date/time.
 * @param ext Three-letter file extension.
 */
static void make_daily_filename(char* out, size_t size, const DateTime& now, const char* ext) {
    if (!out || size == 0) return;
    if (!ext) ext = "TXT";

    snprintf(out, size, "%04d%02d%02d.%s", now.year(), now.month(), now.day(), ext);
}

/**
 * @brief Build a no-RTC FAT 8.3 filename.
 *
 * Format: NORTCnn.EXT
 *
 * @param out Destination buffer.
 * @param size Destination buffer size.
 * @param index Numeric file index from 0 to 99.
 * @param ext Three-letter file extension.
 */
static void make_nortc_filename(char* out, size_t size, uint8_t index, const char* ext) {
    if (!out || size == 0) return;
    if (!ext) ext = "TXT";

    snprintf(out, size, "NORTC%02u.%s", index, ext);
}

/**
 * @brief Select the first available no-RTC filename pair.
 *
 * The function searches for the first pair where neither the CSV nor the LOG
 * file already exists. This prevents overwriting data when the RTC is missing.
 *
 * Generated names are FAT 8.3 compatible:
 * - NORTC00.CSV / NORTC00.LOG
 * - NORTC01.CSV / NORTC01.LOG
 * - ...
 * - NORTC99.CSV / NORTC99.LOG
 *
 * @return true if a free pair was found, false otherwise.
 */
static bool make_nortc_filename_pair() {
    char candidate_data[16] = {0};
    char candidate_log[16]  = {0};

    for (uint8_t i = 0; i < 100; i++) {
        make_nortc_filename(candidate_data, sizeof(candidate_data), i, "CSV");
        make_nortc_filename(candidate_log, sizeof(candidate_log), i, "LOG");

        if (!SD.exists(candidate_data) && !SD.exists(candidate_log)) {
            snprintf(filename_data, sizeof(filename_data), "%s", candidate_data);
            snprintf(filename_log, sizeof(filename_log), "%s", candidate_log);
            return true;
        }
    }

    error_signal(ERR_SD_WRITE_FAIL);
    return false;
}

/**
 * @brief Append one text line to a file.
 *
 * @param filename Target filename.
 * @param line Line to append.
 * @return true on success, false on error.
 */
static bool write_line_to_file(const char* filename, const char* line) {
    if (!filename || filename[0] == '\0' || !line) {
        error_signal(ERR_SD_WRITE_FAIL);
        return false;
    }

    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        error_signal(ERR_SD_WRITE_FAIL);
        return false;
    }

    size_t written = file.println(line);
    file.close();

    if (written == 0) {
        error_signal(ERR_SD_WRITE_FAIL);
        return false;
    }

    return true;
}

/**
 * @brief Create an empty file.
 *
 * @param filename File to create.
 * @return true on success, false on error.
 */
static bool create_empty_file(const char* filename) {
    if (!filename || filename[0] == '\0') {
        error_signal(ERR_SD_WRITE_FAIL);
        return false;
    }

    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        error_signal(ERR_SD_WRITE_FAIL);
        return false;
    }

    file.close();
    return true;
}

/**
 * @brief Create a measurement CSV file and write its header.
 *
 * @param filename Target CSV filename.
 * @return true on success, false on error.
 */
static bool create_data_file_with_header(const char* filename) {
    if (!filename || filename[0] == '\0') {
        error_signal(ERR_SD_WRITE_FAIL);
        return false;
    }

    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        error_signal(ERR_SD_WRITE_FAIL);
        return false;
    }

    size_t written = file.println("timestamp;sensor;value;unit");
    file.close();

    if (written == 0) {
        error_signal(ERR_SD_WRITE_FAIL);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// SD initialization
// ---------------------------------------------------------------------------

/**
 * @brief Initialize SD card communication.
 *
 * @param pin_cs Chip select pin.
 * @return true if the SD card is available, false otherwise.
 */
bool sd_initialization(uint8_t pin_cs) {
    if (!SD.begin(pin_cs)) {
        error_signal(ERR_SD_NOT_FOUND);
        return false;
    }

    LOG_INFO("SD card initialized successfully");
    return true;
}

// ---------------------------------------------------------------------------
// Circular buffer
// ---------------------------------------------------------------------------

/**
 * @brief Add raw characters to the circular SD buffer.
 *
 * If the buffer becomes full, the tail is advanced to preserve the newest data.
 *
 * @param cb Circular buffer pointer.
 * @param line Null-terminated text to append.
 * @return true on success, false if input is invalid.
 */
bool addToCircularBuffer(CircularBuffer* cb, const char* line) {
    static bool overflowWarned = false;

    if (!cb || !line) return false;

    size_t len = strlen(line);

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

    return true;
}

/**
 * @brief Add one complete line to the circular buffer.
 *
 * A newline character is appended after the line.
 *
 * @param cb Circular buffer pointer.
 * @param line Null-terminated line.
 * @return true on success, false on error.
 */
static bool addLineToCircularBuffer(CircularBuffer* cb, const char* line) {
    if (!addToCircularBuffer(cb, line)) return false;
    return addToCircularBuffer(cb, "\n");
}

/**
 * @brief Flush buffered measurement data to the current CSV file.
 *
 * Data is written only when at least one SD sector worth of data is available.
 *
 * @param cb Circular buffer pointer.
 * @return 0 on success or no-op, 1 on failure.
 */
u_int8_t flushCircularBuffer(CircularBuffer* cb) {
    if (!cb) return 1;

    size_t buffered =
        (cb->head >= cb->tail) ? (cb->head - cb->tail) : (BUFFER_SIZE - cb->tail + cb->head);

    if (buffered < 512) return 0;

    File file = SD.open(get_data_filename(), FILE_WRITE);
    if (!file) {
        error_signal(ERR_SD_WRITE_FAIL);
        return 1;
    }

    bool writeSuccess = true;

    if (cb->head > cb->tail) {
        size_t count        = cb->head - cb->tail;
        size_t bytesWritten = file.write((uint8_t*)&cb->buffer[cb->tail], count);
        if (bytesWritten != count) writeSuccess = false;
    } else {
        size_t part1         = BUFFER_SIZE - cb->tail;
        size_t bytesWritten1 = file.write((uint8_t*)&cb->buffer[cb->tail], part1);
        size_t bytesWritten2 = file.write((uint8_t*)&cb->buffer[0], cb->head);
        if (bytesWritten1 != part1 || bytesWritten2 != cb->head) writeSuccess = false;
    }

    file.close();

    if (!writeSuccess) {
        error_signal(ERR_SD_WRITE_FAIL);
        return 1;
    }

    cb->tail = cb->head;
    LOG_INFO("Circular measurement buffer flushed to SD successfully");
    return 0;
}

// ---------------------------------------------------------------------------
// File management
// ---------------------------------------------------------------------------

/**
 * @brief Create or select the daily measurement CSV file.
 *
 * Filename format: YYYYMMDD.CSV
 *
 * @param now Current RTC date/time.
 * @return true on success, false on error.
 */
bool daily_data_file(const DateTime& now) {
    make_daily_filename(filename_data, sizeof(filename_data), now, "CSV");

    if (SD.exists(filename_data)) {
        LOG_INFO("Daily data file already exists: %s", filename_data);
        return true;
    }

    if (!create_data_file_with_header(filename_data)) {
        return false;
    }

    LOG_INFO("Daily data file created: %s", filename_data);
    return true;
}

/**
 * @brief Create or select the daily system log file.
 *
 * Filename format: YYYYMMDD.LOG
 *
 * @param now Current RTC date/time.
 * @return true on success, false on error.
 */
bool daily_log_file(const DateTime& now) {
    make_daily_filename(filename_log, sizeof(filename_log), now, "LOG");

    if (SD.exists(filename_log)) {
        return true;
    }

    if (!create_empty_file(filename_log)) {
        return false;
    }

    LOG_INFO("Daily system log file created: %s", filename_log);
    return true;
}

/**
 * @brief Create or select a no-RTC measurement and log file pair.
 *
 * This function is used when the RTC is not available. It creates a new file
 * pair using the NORTCnn naming scheme.
 *
 * @return true on success, false if no free filename remains or SD write fails.
 */
static bool nortc_file_pair() {
    if (filename_data[0] != '\0' && filename_log[0] != '\0') {
        return true;
    }

    if (!make_nortc_filename_pair()) {
        return false;
    }

    if (!create_empty_file(filename_log)) {
        return false;
    }

    if (!create_data_file_with_header(filename_data)) {
        return false;
    }

    LOG_INFO("No-RTC file pair created: %s / %s", filename_data, filename_log);
    return true;
}

/**
 * @brief Check whether a new file set must be created.
 *
 * With RTC:
 * - Creates or selects daily files.
 * - Rolls over when the day changes.
 *
 * Without RTC:
 * - Creates one no-RTC file pair per boot/session.
 * - Uses the first free NORTCnn.CSV / NORTCnn.LOG pair.
 *
 * @param now Current date/time object. Only trusted when rtc_available is true.
 * @param rtc_available true if RTC date/time is valid.
 * @return true on success, false on error.
 */
bool check_and_create_new_daily_file(const DateTime& now, bool rtc_available) {
    if (!rtc_available) {
        return nortc_file_pair();
    }

    if (now.day() == rtc_state().last_log_day && filename_data[0] != '\0' &&
        filename_log[0] != '\0') {
        return true;
    }

    if (!daily_log_file(now)) {
        return false;
    }

    if (!daily_data_file(now)) {
        return false;
    }

    rtc_state().last_log_day = now.day();

    LOG_DEBUG("Initialization or day change detected. New daily files: %s / %s",
              get_data_filename(),
              get_log_filename());

    return true;
}

// ---------------------------------------------------------------------------
// Measurement logging
// ---------------------------------------------------------------------------

/**
 * @brief Log a single timestamped measurement.
 *
 * Output format:
 * YYYY-MM-DD HH:MM:SS.mmm;sensor_name;value;unit;
 *
 * @param now Current date/time.
 * @param sensor Sensor name.
 * @param value Measured value.
 * @param unit Measurement unit.
 * @param use_buffer true to write to circular buffer, false to write directly.
 * @return true on success, false on write or format error.
 */
bool logMeasurement(
    const DateTime& now, const char* sensor, float value, const char* unit, bool use_buffer) {
    if (!sensor) sensor = "";
    if (!unit) unit = "";

    uint16_t ms = millis() % 1000;
    char line[128];

    int n = snprintf(line,
                     sizeof(line),
                     "%04d-%02d-%02d %02d:%02d:%02d.%03u;%s;%.3f;%s;",
                     now.year(),
                     now.month(),
                     now.day(),
                     now.hour(),
                     now.minute(),
                     now.second(),
                     ms,
                     sensor,
                     value,
                     unit);

    if (n < 0 || n >= (int)sizeof(line)) {
        error_signal(ERR_SD_WRITE_FAIL);
        return false;
    }

    if (use_buffer) {
        return addLineToCircularBuffer(&sdBuffer, line);
    }

    return write_line_to_file(get_data_filename(), line);
}

// ---------------------------------------------------------------------------
// System log event writing
// ---------------------------------------------------------------------------

/**
 * @brief Append a fully formatted system log line to the current LOG file.
 *
 * If the log filename is not ready yet, the message is kept temporarily and
 * written once the file becomes available.
 *
 * @param message Fully formatted log message.
 */
void log_event(const char* message) {
    static char pending[256] = {0};
    static bool hasPending   = false;

    if (!message) return;

    if (get_log_filename()[0] == '\0') {
        snprintf(pending, sizeof(pending), "%s", message);
        hasPending = true;
        return;
    }

    File file = SD.open(get_log_filename(), FILE_WRITE);
    if (!file) {
        snprintf(pending, sizeof(pending), "%s", message);
        hasPending = true;
        error_signal(ERR_SD_WRITE_FAIL);
        return;
    }

    if (hasPending) {
        file.println(pending);
        hasPending = false;
        pending[0] = '\0';
    }

    size_t written = file.println(message);
    file.close();

    if (written == 0) {
        error_signal(ERR_SD_WRITE_FAIL);
    }
}

/** @} */
