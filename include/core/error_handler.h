/**
 * @file error_handler.h
 * @brief Error management system using the built-in LED.
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
#include <stdint.h>

/**
 * @enum ErrorCode
 * @brief Error code definitions for LED-based signaling.
 *
 * Each error code corresponds to a specific failure condition and may
 * be represented by a distinct LED blink pattern.
 *
 * The exact blink encoding is implementation-defined
 * (see error_handler.cpp).
 *
 * @ingroup SystemModules
 */
enum ErrorCode : uint8_t {
    ERR_NONE             = 0,  ///< No error
    ERR_SD_NOT_FOUND     = 2,  ///< SD card not detected
    ERR_SD_WRITE_FAIL    = 3,  ///< Failed to write on SD card (possible corruption)
    ERR_RTC_FAILURE      = 4,  ///< RTC not detected or battery depleted
    ERR_BATTERY_LOW      = 5,  ///< Battery voltage low but not critical
    ERR_BATTERY_CRITICAL = 6,  ///< Battery voltage critically low, shutdown required
    ERR_I2C_NOT_READY    = 7   ///< I2C bus not ready (Wire not initialized or busy)
};

/**
 * @brief Signal an error using LED blink patterns.
 *
 * @details
 * Encodes the provided @ref ErrorCode as a LED blink pattern.
 * Depending on @p halt_system, the function may block indefinitely
 * after signaling the error.
 *
 * This function is designed to be safe to call when:
 * - Serial output is unavailable
 * - SD card logging is unavailable
 * - The system is in a degraded or fatal state
 *
 * @param code        Error code to signal.
 * @param halt_system If true, the system halts indefinitely after signaling.
 *
 * @ingroup SystemModules
 */
void error_signal(ErrorCode code);
