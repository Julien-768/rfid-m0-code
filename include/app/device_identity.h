/**
 * @file device_identity.h
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
 * @brief Persistent factory identity stored in SAMD21 internal flash (FlashStorage).
 *
 * Fields are ASCII, null-terminated.
 */
struct device_identity_t {
    char manufacturer[16];
    char device_type[16];
    char date_fab[16];
    char serial_number[16];
};

static_assert(sizeof(device_identity_t) == 64,
              "device_identity_t size changed: update flash record format if needed.");

/**
 * @brief Initialize identity cache from flash.
 * @return true if valid identity loaded or defaults written, false on flash error.
 */
bool device_id_init();

/**
 * @brief Print the factory identity to the log.
 *
 * @param id Identity payload to print.
 */
void device_id_print(const device_identity_t& id);

/**
 * @brief Get cached identity (RAM).
 * If init failed, returns safe defaults in RAM.
 */
const device_identity_t& device_id_get();

/**
 * @brief Program a new identity into flash (factory).
 * @return true on success, false on write failure.
 */
bool device_id_program(const device_identity_t& id);

/**
 * @brief Factory reset to defaults (optional helper).
 */
bool device_id_resetDefaults();

void device_id_applyFromFields(const char* manufacturer,
                               const char* device_type,
                               const char* date_fab,
                               const char* serial_number);
