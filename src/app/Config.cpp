/**
 * @file config.cpp
 * @brief Runtime configuration handling for the logger.
 *
 * This module defines the global runtime configuration and provides helpers
 * to load options from the SD card and apply configuration received from
 * an external GUI (SET_CONFIG).
 *
 * Two distinct data groups are handled:
 * - @ref Config : Runtime options used directly by the application
 * - @ref TimeSyncConfig : GUI-provided date/time data used only for RTC update
 *
 * ## Typical flow
 * - Boot (autonomous): @ref load_configuration() loads runtime options from
 *   `/config.cfg`.
 * - Connected mode: GUI sends a `SET_CONFIG` payload, applied by
 *   @ref applyGuiConfigAndBuildDateTime(), which:
 *   - updates @ref gui_time_sync with the incoming date/time
 *   - updates @ref config with the incoming runtime options
 *   - returns a `DateTime` used to update the RTC
 *
 * @note JSON parsing uses ArduinoJson v6.
 * @warning SD card must be initialized before calling @ref load_configuration().
 */

#include <ArduinoJson.h>
#include <SD.h>
#include "config.h"
#include "log.h"
#include "utils.h"
#include "JsonProtocol.h"

/**
 * @var config
 * @brief Global runtime configuration options.
 *
 * This structure represents the effective configuration used by the application
 * (state machine, deploy loop, sensor enabling, RFID handling, etc.).
 */
Config config;

/**
 * @var gui_time_sync
 * @brief Latest GUI-provided date/time payload.
 *
 * This structure stores the last date/time received from the external GUI.
 * It is separate from @ref config because date/time synchronization is not a
 * persistent runtime behavior parameter.
 */
TimeSyncConfig gui_time_sync = {};

/**
 * @var gui_time_sync_rb
 * @brief Readback date/time payload.
 *
 * Intended to store date/time values returned to the GUI, if required for
 * verification or reporting purposes.
 */
TimeSyncConfig gui_time_sync_rb = {};

/**
 * @brief Load runtime configuration from `/config.cfg` on the SD card.
 *
 * This function:
 * - opens the `/config.cfg` file from the SD card
 * - parses it as JSON using ArduinoJson
 * - updates the provided @ref Config instance with runtime options
 * - leaves existing values in place if the file is missing or invalid
 *
 * Example `/config.cfg`:
 * @code{.json}
 * {
 *   "use_buffer": false,
 *   "enable_light1": false,
 *   "enable_light2": false,
 *   "enable_rfid": true,
 *   "rfid_mode": 2,
 *   "enable_vbat": true,
 *   "acquisition_interval_s": 120
 * }
 * @endcode
 *
 * Supported keys:
 * - `use_buffer`: Use circular-buffered SD logging
 * - `enable_light1`: Enable AS7341 spectral sensor
 * - `enable_light2`: Enable TSL2591 ambient light sensor
 * - `enable_rfid`: Enable RFID reader
 * - `rfid_mode`: RFID operating mode (0=OFF, 1=CONTINUOUS, 2=ON_IR_EVENT)
 * - `enable_vbat`: Enable battery voltage measurement
 * - `acquisition_interval_s`: Sensor acquisition interval in seconds
 *
 * @param config_var Reference to the @ref Config structure to update.
 * @return true if the file was found and parsed successfully, false otherwise.
 *
 * @warning The SD card must be initialized before calling this function.
 * @note Unknown JSON keys are ignored. Missing keys keep current values via
 *       ArduinoJson's `|` operator.
 */
bool load_configuration(Config& config_var) {
    File file = SD.open("/config.cfg");
    if (!file) {
        LOG_WARN("config.cfg not found on SD card. Using defaults.");
        return false;
    }

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        LOG_ERROR("Failed to parse config.cfg: %s", error.c_str());
        file.close();
        return false;
    }

    // Update configuration fields with JSON values or keep existing defaults
    config_var.use_buffer    = doc["use_buffer"] | config_var.use_buffer;
    config_var.enable_light1 = doc["enable_light1"] | config_var.enable_light1;
    config_var.enable_light2 = doc["enable_light2"] | config_var.enable_light2;
    config_var.enable_rfid   = doc["enable_rfid"] | config_var.enable_rfid;
    config_var.rfid_mode     = doc["rfid_mode"] | config_var.rfid_mode;
    config_var.enable_vbat   = doc["enable_vbat"] | config_var.enable_vbat;
    config_var.acquisition_interval_s =
        doc["acquisition_interval_s"] | config_var.acquisition_interval_s;

    file.close();

    // Compact summary log of effective configuration
    LOG_INFO("Configuration loaded:");
    LOG_INFO(config_var.use_buffer ? "\tuse_buffer: true" : "\tuse_buffer: false");
    LOG_INFO(config_var.enable_light1 ? "\tlight1: true" : "\tlight1: false");
    LOG_INFO(config_var.enable_light2 ? "\tlight2: true" : "\tlight2: false");
    LOG_INFO(config_var.enable_rfid ? "\trfid: true" : "\trfid: false");
    LOG_INFO("\trfid_mode: %u", config_var.rfid_mode);
    LOG_INFO(config_var.enable_vbat ? "\tvbat: true" : "\tvbat: false");
    LOG_INFO("\tinterval: %u s", config_var.acquisition_interval_s);

    return true;
}

/**
 * @brief Apply GUI-provided runtime settings and build a DateTime for RTC update.
 *
 * This helper applies a `SET_CONFIG` payload received from an external GUI:
 * - converts the ISO8601-like date string into the internal BCD date representation
 *   stored in @ref gui_time_sync
 * - updates the runtime @ref config fields from the GUI payload
 * - builds a `DateTime` object for the RTC layer
 *
 * @param src Incoming SET_CONFIG payload (typically parsed from the JSON protocol).
 * @return A `DateTime` representing the requested current date/time.
 *
 * @note This function does not directly update the RTC. The caller should pass the
 *       returned `DateTime` to the RTC module.
 *
 * @warning The interpretation of the incoming date string depends on
 *          @ref convertDatetoBcd(). Ensure the expected format is enforced by the GUI.
 */
DateTime applyGuiConfigAndBuildDateTime(const SetConfigPayload& src) {
    // Store the GUI-provided current date/time
    convertDatetoBcd(src.dateCurrentIso, &gui_time_sync.dateCurrent);

    // Log the received date/time in human-readable form
    char dateCurrentStr[24];
    convertBcdDateToISO8601(&gui_time_sync.dateCurrent, dateCurrentStr, sizeof(dateCurrentStr));
    LOG_INFO("Date/time set to: %s", dateCurrentStr);

    // Build DateTime for RTC update
    DateTime dt(2000 + bcdToDec(gui_time_sync.dateCurrent.year),
                bcdToDec(gui_time_sync.dateCurrent.month), bcdToDec(gui_time_sync.dateCurrent.day),
                bcdToDec(gui_time_sync.dateCurrent.hour),
                bcdToDec(gui_time_sync.dateCurrent.minute),
                bcdToDec(gui_time_sync.dateCurrent.second));

    // Update runtime configuration directly
    config.acquisition_interval_s = src.acquisition_interval_s;
    config.enable_light1          = src.enable_light1;
    config.enable_light2          = src.enable_light2;
    config.rfid_mode              = src.rfid_mode;
    config.enable_vbat            = src.enable_vbat;

    // Short readable summary
    LOG_DEBUG("Updated configuration from GUI:");
    LOG_DEBUG(config.enable_light1 ? "  light1: enabled" : "  light1: disabled");
    LOG_DEBUG(config.enable_light2 ? "  light2: enabled" : "  light2: disabled");

    LOG_DEBUG("  rfid_mode: %u", config.rfid_mode);
    LOG_DEBUG(config.enable_vbat ? "  vbat: enabled" : "  vbat: disabled");
    LOG_DEBUG("  interval (s): %u", config.acquisition_interval_s);

    return dt;
}
