/**
 * @file config.h
 * @brief Runtime configuration model for the device.
 *
 * @version 3.0.0
 * @date 2022-08-17
 *
 * @section authors Authors
 * - Julien Courtecuisse
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: julien.courtecuisse@iphc.cnrs.fr
 *
 * @section project Project
 * Name: RFID-M0
 * Description: Embedded firmware for the open-source RFID-M0 project, designed to monitor small animal species around burrows and nest boxes. The software integrates RFID-based individual identification, infrared passage detection, real-time clock (RTC) timestamping, and SD-card data logging, with low-power management for autonomous field deployments. It supports ecological research, including long-term monitoring of Great Tits (Parus major) and research on the European hamster (Cricetus cricetus) conducted within the cross-border CRICETUS project.
 *
 * @section contributors Contributors
 * - Robin Laesser
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: robin.laesser@iphc.cnrs.fr
 *
 * @section license License
 * Copyright (C) 2022 Julien Courtecuisse
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * License: GPL-3.0-or-later
 * https://www.gnu.org/licenses/gpl-3.0.html
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
