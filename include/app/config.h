/**
 * @file config.h
 * @brief Runtime configuration model for the Moonraker logger.
 *
 * This module defines the configuration structures used across the system
 * to enable/disable hardware sensors, adjust acquisition timing, and exchange
 * configuration parameters with the external GUI.
 *
 * It contains:
 * - @ref Config : The main runtime configuration loaded from `config.cfg`
 * - @ref Cfg_t  : The serialized configuration payload exchanged over UART
 * - Global instances for pending and read-back configuration (@ref Cfg_p, @ref Cfg_rb)
 * - @ref loadConfiguration() : Function to load configuration from SD card
 *
 * @ingroup ConfigLayer
 */

#pragma once

#include <Arduino.h>
#include "drivers/rtc.h"
#include "JsonProtocol.h"

/**
 * @struct Config
 * @brief Runtime system configuration loaded from SD card.
 *
 * This structure contains all user-adjustable parameters that affect the
 * logger’s behavior during DEPLOY mode. It is populated once at startup
 * via @ref loadConfiguration().
 *
 * ### Fields
 * - **use_buffer** — Enable or disable circular-buffered SD logging
 * - **enable_light1** — Enable AS7341 spectral sensor
 * - **enable_light2** — Enable TSL2591 ambient light sensor
 * - **enable_vbat** — Enable battery voltage monitoring
 * - **acquisition_interval_s** — Time interval (in seconds) between readings
 *
 * Typical usage:
 * @code
 * loadConfiguration(config);
 *
 * if (config.enable_light1) {
 *     // Acquire AS7341 readings
 * }
 * @endcode
 */
struct Config
{
    bool use_buffer                  = false;  ///< Use circular-buffered SD logging
    bool enable_light1               = true;   ///< Enable AS7341 spectral sensor
    bool enable_light2               = false;  ///< Enable TSL2591 ambient light sensor
    bool enable_vbat                 = true;   ///< Enable battery voltage measurement
    u_int16_t acquisition_interval_s = 120;    ///< Sensor acquisition interval (seconds)
};

/**
 * @brief Global runtime configuration instance.
 *
 * After calling @ref loadConfiguration, this object contains all runtime
 * parameters used across the system (sensor enable flags, interval, etc.).
 *
 * @ingroup ConfigLayer
 */
extern Config config;

/**
 * @struct Cfg_t
 * @brief Serialized configuration payload exchanged with the external GUI.
 *
 * This structure is used when receiving or transmitting configuration packets
 * over UART (typically via SET_CONFIG / GET_CONFIG commands).
 *
 * It includes both deployment parameters and a timestamp provided by the host.
 *
 * ### Notes
 * - The structure is kept minimal for efficient UART transfer.
 * - It mirrors the fields stored in `config.cfg`.
 *
 * @ingroup ConfigLayer
 */
typedef struct
{
    LoggerTime_t dateCurrent;         ///< Host-provided current date/time
    uint16_t acquisition_interval_s;  ///< Acquisition interval in seconds
    bool enable_light1;               ///< Enable AS7341 sensor
    bool enable_light2;               ///< Enable TSL2591 sensor
    bool enable_vbat;                 ///< Enable battery monitoring
} Cfg_t;

/**
 * @brief Pending configuration received from the external GUI.
 *
 * Filled when a SET_CONFIG command is received. The application layer is
 * responsible for applying these values (writing RTC, saving SD config, etc.).
 *
 * @ingroup ConfigLayer
 */
extern Cfg_t Cfg_p;

/**
 * @brief Configuration payload used when sending parameters back to the GUI.
 *
 * Sent in response to GET_CONFIG requests.
 *
 * @ingroup ConfigLayer
 */
extern Cfg_t Cfg_rb;

/**
 * @brief Load the configuration file (`config.cfg`) from the SD card.
 *
 * Parses the JSON configuration file, updates the provided @ref Config
 * structure, and logs any missing or malformed fields.
 *
 * @param config Reference to the global runtime configuration.
 *
 * @note Invalid or missing fields fall back to default values.
 * @note Logging is performed using the @c LOG_* macros.
 *
 * @ingroup ConfigLayer
 */
bool loadConfiguration(Config& config);

/// Apply GUI configuration payload to persistent + runtime config,
/// and build the corresponding DateTime for the RTC.
DateTime applyGuiConfigAndBuildDateTime(const SetConfigPayload& src);
