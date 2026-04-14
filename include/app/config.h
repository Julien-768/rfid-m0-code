/**
 * @file config.h
 * @brief Runtime configuration model for the logger.
 *
 * This module defines the configuration structures used by the application.
 *
 * It contains:
 * - @ref Config : The single runtime configuration source used by the application
 * - @ref TimeSyncConfig : Date/time payload received from the external GUI
 * - Global instances for runtime configuration and GUI-provided time sync data
 * - @ref load_configuration() : Function to load configuration from SD card
 * - @ref applyGuiConfigAndBuildDateTime() : Function to apply GUI runtime settings
 *   and build a DateTime object for RTC update
 *
 * @ingroup ConfigLayer
 */

#pragma once

#include <Arduino.h>
#include "rtc.h"
#include "JsonProtocol.h"

/**
 * @struct Config
 * @brief Runtime system configuration loaded from SD card or updated from the GUI.
 *
 * This structure contains all user-adjustable parameters that directly affect
 * logger behavior during runtime. It is the single source of truth for the
 * application configuration.
 *
 * Typical sources:
 * - SD card configuration file at boot via @ref load_configuration()
 * - GUI-provided configuration payload at runtime via
 *   @ref applyGuiConfigAndBuildDateTime()
 *
 * ### Fields
 * - **use_buffer** - Enable or disable circular-buffered SD logging
 * - **enable_light1** - Enable AS7341 spectral sensor
 * - **enable_light2** - Enable TSL2591 ambient light sensor
 * - **enable_rfid** - Enable RFID reader
 * - **rfid_mode** - RFID operating mode
 * - **enable_vbat** - Enable battery voltage monitoring
 * - **acquisition_interval_s** - Time interval in seconds between acquisitions
 *
 * Typical usage:
 * @code
 * load_configuration(config);
 *
 * if (config.enable_light1) {
 *     // Acquire AS7341 readings
 * }
 * @endcode
 */
struct Config {
    bool use_buffer                 = false;  ///< Enable circular-buffered SD logging
    bool enable_light1              = false;  ///< Enable AS7341 spectral sensor
    bool enable_light2              = false;  ///< Enable TSL2591 ambient light sensor
    bool enable_rfid                = true;   ///< Enable RFID reader
    uint8_t rfid_mode               = 2;      ///< RFID mode (0=OFF, 1=CONTINUOUS, 2=ON_IR_EVENT)
    bool enable_vbat                = true;   ///< Enable battery voltage measurement
    uint16_t acquisition_interval_s = 120;    ///< Acquisition interval in seconds
};

/**
 * @brief Global runtime configuration instance.
 *
 * After calling @ref load_configuration(), this object contains the effective
 * runtime parameters used by the application.
 *
 * @ingroup ConfigLayer
 */
extern Config config;

/**
 * @struct TimeSyncConfig
 * @brief GUI-provided date/time payload stored in RAM.
 *
 * This structure stores the most recent date/time received from the external GUI.
 * It is intentionally kept separate from @ref Config because it does not describe
 * runtime logger behavior; it is only used to update the RTC.
 *
 * @ingroup ConfigLayer
 */
struct TimeSyncConfig {
    LoggerTime_t dateCurrent;  ///< Current date/time provided by the host
};

/**
 * @brief Latest date/time payload received from the external GUI.
 *
 * Filled when a SET_CONFIG command is received and used to build the DateTime
 * object passed to the RTC layer.
 *
 * @ingroup ConfigLayer
 */
extern TimeSyncConfig gui_time_sync;

/**
 * @brief Readback date/time payload, if needed for reporting or verification.
 *
 * This can be used when the firmware needs to expose a date/time value back to
 * the external GUI.
 *
 * @ingroup ConfigLayer
 */
extern TimeSyncConfig gui_time_sync_rb;

/**
 * @brief Load the configuration file (`/config.cfg`) from the SD card.
 *
 * Parses the JSON configuration file and updates the provided @ref Config
 * structure. Missing or invalid fields leave existing values unchanged.
 *
 * @param config_var Reference to the runtime configuration structure.
 * @return true if the configuration file was successfully parsed, false otherwise.
 *
 * @note Invalid or missing fields fall back to existing values.
 * @note Logging is performed using the @c LOG_* macros.
 *
 * @ingroup ConfigLayer
 */
bool load_configuration(Config& config_var);

/**
 * @brief Apply GUI-provided runtime settings and build a DateTime for RTC update.
 *
 * This function:
 * - stores the GUI-provided current date/time into @ref gui_time_sync
 * - updates the runtime @ref config fields from the GUI payload
 * - returns a DateTime object ready to be applied to the RTC
 *
 * @param src Incoming SET_CONFIG payload.
 * @return A DateTime representing the GUI-provided current date/time.
 *
 * @ingroup ConfigLayer
 */
DateTime applyGuiConfigAndBuildDateTime(const SetConfigPayload& src);
