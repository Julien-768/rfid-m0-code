/**
 * @file gui_serial.cpp
 * @brief GUI UART ownership and control helpers.
 *
 * This module manages the UART interface used by the external GUI during
 * CONNECTED mode.
 *
 * Responsibilities:
 * - Start GUI UART communication
 * - Stop and release the UART
 * - Centralize UART ownership switching logic
 *
 * The GUI UART may share the same hardware serial peripheral as other runtime
 * modules such as the RFID driver.
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

#include "gui_serial.h"

#include "hardware_serial_control.h"
#include "log.h"

static constexpr uint32_t GUI_BAUDRATE = 115200;

/**
 * @brief Enable or disable GUI UART communication.
 *
 * When enabled:
 * - Restarts the UART interface
 * - Configures the GUI baudrate
 * - Flushes stale RX bytes
 *
 * When disabled:
 * - Flushes pending TX bytes
 * - Stops the UART peripheral
 *
 * This helper centralizes GUI UART ownership transitions.
 *
 * @param serial   Hardware serial interface used by the GUI.
 * @param enabled  Desired UART state.
 */
void gui_serial_set_enabled(HardwareSerial* serial, bool enabled) {
    if (!serial) {
        return;
    }

    if (enabled) {
        HardwareSerialControl::restart(serial, GUI_BAUDRATE, 100);
        HardwareSerialControl::flushRx(serial);

        LOG_DEBUG("GUI UART started");
    } else {
        HardwareSerialControl::stop(serial);

        LOG_DEBUG("GUI UART stopped");
    }
}
