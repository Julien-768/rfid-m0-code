/**
 * @file hardware_serial_control.h
 * @brief Generic HardwareSerial control helpers.
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

namespace HardwareSerialControl {

/**
 * @brief Restart a hardware serial interface safely.
 *
 * Performs the following sequence:
 * - stop the UART peripheral
 * - wait a short stabilization delay
 * - restart the UART with the requested baudrate
 * - wait for UART stabilization
 *
 * This helper is useful when reassigning UART ownership between runtime
 * modules or after external device power cycling.
 *
 * @param serial     Hardware serial interface.
 * @param baudrate   UART baudrate.
 * @param settle_ms  Stabilization delay after restart in milliseconds.
 */
void restart(HardwareSerial* serial, uint32_t baudrate, uint32_t settle_ms = 100);

/**
 * @brief Stop a hardware serial interface safely.
 *
 * Flushes pending TX bytes before stopping the UART peripheral.
 *
 * @param serial Hardware serial interface.
 */
void stop(HardwareSerial* serial);

/**
 * @brief Flush all pending RX bytes from a hardware serial interface.
 *
 * Removes all currently available bytes from the UART RX buffer.
 *
 * Useful after:
 * - external peripheral startup
 * - UART reassignment
 * - line noise or partial frame reception
 *
 * @param serial Hardware serial interface.
 */
void flushRx(HardwareSerial* serial);

}  // namespace HardwareSerialControl
