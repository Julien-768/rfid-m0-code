/**
 * @file config.cpp
 * @brief Runtime configuration handling for the Moonraker logger.
 *
 * This module defines the global configuration structures and provides helpers
 * to load runtime options from the SD card, and to apply configuration received
 * from an external GUI (SET_CONFIG).
 *
 * Two configuration layers coexist:
 * - @ref Config : Runtime options used by the application loop (loaded from SD card).
 * - @ref Cfg_t  : Low-level configuration payload used by the communication protocol
 *                 (stored in RAM after SET_CONFIG reception).
 *
 * ## Typical flow
 * - Boot (autonomous): @ref load_configuration() loads runtime options from `/config.cfg`.
 * - Connected mode: GUI sends a `SET_CONFIG` payload, applied by
 *   @ref applyGuiConfigAndBuildDateTime(), which updates:
 *   - @ref Cfg_p (protocol-facing configuration copy)
 *   - @ref config (runtime configuration)
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
 * (state machine, deploy loop, sensor enabling, etc.).
 *
 * It is typically loaded from `/config.cfg` on the SD card using
 * @ref load_configuration(), and may be updated at runtime from GUI commands
 * (see @ref applyGuiConfigAndBuildDateTime()).
 */
Config config;  // Actual allocation of the global variable

/**
 * @var Cfg_p
 * @brief Primary protocol configuration stored in RAM.
 *
 * This is filled when a SET_CONFIG command is received over the communication link.
 * It stores the most recent configuration as defined by the external controller/GUI.
 *
 * @note This structure is distinct from @ref config (runtime configuration).
 */
Cfg_t Cfg_p = {};

/**
 * @var Cfg_rb
 * @brief Readback protocol configuration.
 *
 * Intended to store configuration read back from the logger hardware (if applicable),
 * for verification or reporting purposes.
 */
Cfg_t Cfg_rb = {};

/**
 * @brief Load runtime configuration from `/config.cfg` on the SD card.
 *
 * This function:
 * - Opens the `/config.cfg` file from the SD card.
 * - Parses it as JSON using ArduinoJson.
 * - Updates the provided @ref Config instance with runtime options.
 * - Leaves existing defaults in place if the file is missing or invalid.
 *
 * Example `/config.cfg`:
 * @code{.json}
 * {
 *   "use_buffer": true,
 *   "enable_light1": true,
 *   "enable_light2": true,
 *   "enable_vbat": true,
 *   "acquisition_interval_s": 60
 * }
 * @endcode
 *
 * @param config Reference to the @ref Config structure to update.
 * @return true if the file was found and parsed successfully, false otherwise.
 *
 * @warning The SD card must be initialized before calling this function.
 * @note Unknown JSON keys are ignored. Missing keys keep current defaults via
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
    config_var.enable_vbat   = doc["enable_vbat"] | config_var.enable_vbat;
    config_var.acquisition_interval_s =
        doc["acquisition_interval_s"] | config_var.acquisition_interval_s;

    file.close();

    // Compact summary log of effective configuration
    LOG_INFO("Configuration loaded:");
    LOG_INFO(config_var.use_buffer ? "  use_buffer: true" : "  use_buffer: false");
    LOG_INFO(config_var.enable_light1 ? "  light1: true" : "  light1: false");
    LOG_INFO(config_var.enable_light2 ? "  light2: true" : "  light2: false");
    LOG_INFO(config_var.enable_vbat ? "  vbat: true" : "  vbat: false");
    LOG_INFO("  interval: %u s", config_var.acquisition_interval_s);

    return true;
}

/**
 * @brief Apply GUI-provided configuration and build a `DateTime` for RTC update.
 *
 * This helper applies a `SET_CONFIG` payload received from an external GUI:
 * - Converts the ISO8601-like date string into the internal BCD date representation
 *   stored in @ref Cfg_p.
 * - Copies sensor enable flags and acquisition interval into @ref Cfg_p.
 * - Builds a `DateTime` from @ref Cfg_p.dateCurrent (to be passed to the RTC layer).
 * - Updates the runtime @ref config fields so the application immediately uses the
 *   new interval and enable flags.
 *
 * @param src Incoming SET_CONFIG payload (typically parsed from JSON protocol).
 * @return A `DateTime` representing the requested current date/time.
 *
 * @note This function does not directly update the RTC. The caller should pass the
 *       returned `DateTime` to the RTC module (e.g., `rtc_applyExternalTime()`).
 *
 * @warning The interpretation of the incoming date string depends on
 *          @ref convertDatetoBcd(). Ensure the expected format is enforced by the GUI.
 */
DateTime applyGuiConfigAndBuildDateTime(const SetConfigPayload& src) {
    // --- Copy into persistent protocol configuration (Cfg_p) ---
    convertDatetoBcd(src.dateCurrentIso, &Cfg_p.dateCurrent);
    Cfg_p.acquisition_interval_s = src.acquisition_interval_s;
    Cfg_p.enable_light1          = src.enable_light1;
    Cfg_p.enable_light2          = src.enable_light2;
    Cfg_p.enable_vbat            = src.enable_vbat;

    // --- Log the received date/time (human-readable) ---
    char dateCurrentStr[24];
    convertBcdDateToISO8601(&Cfg_p.dateCurrent, dateCurrentStr, sizeof(dateCurrentStr));
    LOG_INFO("Date/time set to: %s", dateCurrentStr);

    // --- Build DateTime for RTC update ---
    DateTime dt(2000 + bcdToDec(Cfg_p.dateCurrent.year), bcdToDec(Cfg_p.dateCurrent.month),
                bcdToDec(Cfg_p.dateCurrent.day), bcdToDec(Cfg_p.dateCurrent.hour),
                bcdToDec(Cfg_p.dateCurrent.minute), bcdToDec(Cfg_p.dateCurrent.second));

    // --- Update runtime configuration (Config) ---
    config.acquisition_interval_s = Cfg_p.acquisition_interval_s;
    config.enable_light1          = Cfg_p.enable_light1;
    config.enable_light2          = Cfg_p.enable_light2;
    config.enable_vbat            = Cfg_p.enable_vbat;

    // --- Short readable summary ---
    LOG_DEBUG("Updated configuration from GUI:");
    LOG_DEBUG(config.enable_light1 ? "  light1: enabled" : "  light1: disabled");
    LOG_DEBUG(config.enable_light2 ? "  light2: enabled" : "  light2: disabled");
    LOG_DEBUG(config.enable_vbat ? "  vbat: enabled" : "  vbat: disabled");
    LOG_DEBUG("  interval (s): %u", config.acquisition_interval_s);

    return dt;
}
