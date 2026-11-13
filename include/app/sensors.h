/**
 * @file sensors.h
 *
 * @section authors Authors
 * - Julien Courtecuisse
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: julien.courtecuisse@iphc.cnrs.fr
 *
 * @section project Project
 * Name: RFID-M0
 * Description: Embedded firmware for the open-source RFID-M0 project, designed to monitor small animal species around burrows and nest boxes. The software integrates RFID-based individual identification, infrared passage detection, real-time clock (RTC) timestamping, and SD-card data logging, with low-power management for autonomous field deployments. It supports ecological research, including long-term monitoring of Great Tits (Parus major) and research on the European hamster (Cricetus cricetus) conducted within the cross-border CRICETUS project.
 * @version 3.0.0
 * @date 2022-08-17
 *
 * @section contributors Contributors
 * - Robin Laesser
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: robin.laesser@iphc.cnrs.fr
 *
 * @section license License
 * SPDX-FileCopyrightText: 2022 Julien Courtecuisse
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
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
#include <RTClib.h>
#include "battery.h"
#include "sensors_hal.h"

/**
 * @brief Unified frame holding all sensor readings and validity flags.
 */
struct SensorFrame {
    // AS7341Reading as7341;
    // bool valid_as7341 = false;

    // TSL2591Reading tsl2591;
    // bool valid_tsl2591 = false;

    bool battery_valid = false;
    int32_t vbat_mv    = 0.0f;
};

/**
 * @brief Read all enabled sensors described in a SensorSpec table.
 *
 * @param table Pointer to the array of SensorSpec.
 * @param count Number of elements in @p table.
 * @return Filled SensorFrame containing all measured values.
 */
SensorFrame readAllSensors(SensorSpec* table, size_t count);

/**
 * @brief Initialize all sensors listed in the SensorSpec table.
 */
void Sensors_InitForDeploy(SensorSpec* table, size_t count);

/**
 * @brief Log all values of a SensorFrame to SD card.
 */
void logSensorFrame(const DateTime& now, const SensorFrame& f);
