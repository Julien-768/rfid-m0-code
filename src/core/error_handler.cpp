/**
 * @file error_handler.cpp
 * @brief Implementation of the LED-based error signaling system.
 *
 * This file implements the runtime behavior of the error signaling mechanism
 * declared in @ref error_handler.h.
 *
 * Errors are reported using LED blink patterns corresponding to the numeric
 * value of the @ref ErrorCode. Optionally, the system may halt indefinitely
 * after signaling a fatal error.
 *
 * This module is designed for last-resort diagnostics when logging or
 * serial communication may not be available.
 *
 * @see error_handler.h
 * @ingroup SystemModules
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

#include <Arduino.h>

#include "error_handler.h"
#include "log.h"
#include "signal.h"
#include "hardware.h"

namespace {
/// Return a safe, non-negative blink count from an ErrorCode.
inline uint8_t blink_count(ErrorCode code) {
    const int v = static_cast<int>(code);
    return (v > 0) ? static_cast<uint8_t>(v) : 0u;
}
}  // namespace

/**
 * @brief Log and signal an error using LED blink patterns and optional system halt.
 *
 * The error code is represented by a number of LED blinks equal to the
 * numeric value of @p code.
 *
 * If @p halt_system is true, the function does not return and the system
 * remains halted after signaling the error.
 *
 * @param code         Error code to signal.
 */
void error_signal(ErrorCode code) {
    LOG_ERROR("Error code %d", static_cast<int>(code));
    blink_blocking_safe(PIN_BUZZER_LED, 200, 200, blink_count(code));
    // led_start_blink_isr(blink_count(code), blink_mode::fast);
}
