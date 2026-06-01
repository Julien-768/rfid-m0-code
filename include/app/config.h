/**
 * @file config.h
 * @brief Runtime configuration model for the logger.
 *
 * This module defines the runtime configuration used by the application.
 *
 * Responsibilities:
 * - define configuration data structures
 * - define default values
 * - validate and sanitize configuration values
 * - load configuration from the SD card
 *
 * This module does not parse GUI JSON commands and does not build JSON replies.
 *
 * @ingroup ConfigLayer
 */

#pragma once

#include <Arduino.h>

/**
 * @enum RfidMode
 * @brief Supported RFID operating modes.
 */
enum class RfidMode : uint8_t { Off = 0, Continuous = 1, OnIrEvent = 2 };

constexpr RfidMode DEFAULT_RFID_MODE = RfidMode::OnIrEvent;

constexpr uint16_t DEFAULT_ACQUISITION_INTERVAL_S = 120;
constexpr uint16_t MIN_ACQUISITION_INTERVAL_S     = 1;

constexpr uint8_t DEFAULT_SCHEDULE_START_HOUR   = 8;
constexpr uint8_t DEFAULT_SCHEDULE_START_MINUTE = 0;
constexpr uint8_t DEFAULT_SCHEDULE_END_HOUR     = 18;
constexpr uint8_t DEFAULT_SCHEDULE_END_MINUTE   = 30;

/**
 * @struct Config
 * @brief Runtime system configuration loaded from SD card or updated by the application.
 */
struct Config {
    bool use_buffer                 = false;
    bool enable_ir1                 = false;
    bool enable_ir2                 = false;
    bool enable_rfid                = true;
    RfidMode rfid_mode              = DEFAULT_RFID_MODE;
    bool enable_vbat                = true;
    uint16_t acquisition_interval_s = DEFAULT_ACQUISITION_INTERVAL_S;

    uint8_t schedule_start_hour   = DEFAULT_SCHEDULE_START_HOUR;
    uint8_t schedule_start_minute = DEFAULT_SCHEDULE_START_MINUTE;
    uint8_t schedule_end_hour     = DEFAULT_SCHEDULE_END_HOUR;
    uint8_t schedule_end_minute   = DEFAULT_SCHEDULE_END_MINUTE;
};

/**
 * @brief Global runtime configuration instance.
 */
extern Config config;

uint8_t rfidModeToUint(RfidMode mode);
RfidMode rfidModeFromUint(uint8_t value);

bool validateConfig(Config& config_var);
bool load_configuration(Config& config_var);
