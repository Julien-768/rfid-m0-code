/**
 * @file sd_manager.cpp
 * @defgroup SD_Manager SD Manager
 * @ingroup SystemModules
 * @brief SD card management and buffered data logging for the Logger.
 *
 * The **SD Manager** module handles initialization, daily file management, and
 * buffered writing for the Logger. It ensures reliable, power-efficient logging
 * even under intermittent storage access or during long deployments.
 *
 * ## Responsibilities
 * - Initialize and verify SD card communication.
 * - Manage a circular buffer for batched measurement writes.
 * - Create and maintain daily measurement and system log files.
 * - Write measurement data from sensors to a CSV file.
 * - Write system logs to a separate LOG file.
 * - Handle SD write failures and transition to safe shutdown (END-OF-LIFE).
 *
 * ## Daily files
 * - Measurements: YYYYMMDD.CSV
 * - System logs:  YYYYMMDD.LOG
 *
 * ## Error Handling
 * - On initialization failure -> @ref ERR_SD_NOT_FOUND, enters `STATE_ENDOFLIFE`.
 * - On write or flush error -> @ref ERR_SD_WRITE_FAIL, enters `STATE_ENDOFLIFE`.
 *
 * @{
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
// 8.3 FAT-compatible names: YYYYMMDD.CSV / YYYYMMDD.LOG
// ---------------------------------------------------------------------------
char filename_data[16] = {0};
char filename_log[16]  = {0};

const char* get_data_filename() {
    return filename_data;
}

const char* get_log_filename() {
    return filename_log;
}

// Define the global CircularBuffer object.
CircularBuffer sdBuffer_instance;

CircularBuffer& get_sdBuffer() {
    return sdBuffer_instance;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
static void make_daily_filename(char* out, size_t size, const DateTime& now, const char* ext) {
    if (!out || size == 0) return;
    if (!ext) ext = "TXT";

    snprintf(out, size, "%04d%02d%02d.%s", now.year(), now.month(), now.day(), ext);
}

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

// ---------------------------------------------------------------------------
// SD initialization
// ---------------------------------------------------------------------------
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

static bool addLineToCircularBuffer(CircularBuffer* cb, const char* line) {
    if (!addToCircularBuffer(cb, line)) return false;
    return addToCircularBuffer(cb, "\n");
}

/**
 * @brief Flush buffered measurement data to the daily CSV file.
 *
 * Writes data from the circular buffer to the SD card when at least one
 * complete SD sector worth of data is available. The buffer contains CSV
 * measurement lines only, not system logs.
 *
 * @return 0 on success/no-op, 1 on failure.
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
// Daily file management
// ---------------------------------------------------------------------------
/**
 * @brief Create or select the daily measurement CSV file.
 *
 * Filename format: YYYYMMDD.CSV
 * CSV format: timestamp;sensor;value;unit
 */
bool daily_data_file(const DateTime& now) {
    make_daily_filename(filename_data, sizeof(filename_data), now, "CSV");

    if (SD.exists(filename_data)) {
        LOG_INFO("Daily data file already exists: %s", filename_data);
        return true;
    }

    File file = SD.open(filename_data, FILE_WRITE);
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

    LOG_INFO("Daily data file created: %s", filename_data);
    return true;
}

/**
 * @brief Create or select the daily system log file.
 *
 * Filename format: YYYYMMDD.LOG
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

bool check_and_create_new_daily_file(const DateTime& now) {
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
 * @brief Log a single timestamped measurement to CSV file or buffer.
 *
 * Output format:
 * @code
 * YYYY-MM-DD HH:MM:SS.mmm;sensor_name;value;unit;
 * @endcode
 *
 * @return true on success, false on write/format error.
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
 * @brief Append a fully formatted system log line to the daily LOG file.
 *
 * The timestamp and level are already formatted by log.cpp. This function only
 * selects the system log file and appends the received line.
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

/** @} */  // end of SD_Manager group
