/**
 * @file mcu_uid.cpp
 * @brief Read MCU unique ID for Feather M0
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

#include "mcu_uid.h"
#include "log.h"

String mcu_uid_read() {
    LOG_DEBUG("Reading Feather M0 UID from internal flash...");

    const uint32_t uid[4] = {*(uint32_t*)0x0080A00C,
                             *(uint32_t*)0x0080A040,
                             *(uint32_t*)0x0080A044,
                             *(uint32_t*)0x0080A048};

    char uid_str[64];
    snprintf(uid_str,
             sizeof(uid_str),
             "FeatherM0_0x%08lX_0x%08lX_0x%08lX_0x%08lX",
             uid[0],
             uid[1],
             uid[2],
             uid[3]);
    LOG_DEBUG("Feather M0 UID read: %08lX-%08lX-%08lX-%08lX", uid[0], uid[1], uid[2], uid[3]);

    return uid_str;
}
