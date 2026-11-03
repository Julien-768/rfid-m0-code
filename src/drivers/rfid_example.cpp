/**
 * @file rfid_example.cpp
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
#include "rfid_driver.h"

rfid_driver_t rfid1;
rfid_driver_t rfid2;  // optional second instance

tag_info_t prev1 = {{0}, 0};

void rfid_example_setup() {
    Serial.begin(115200);

    Serial1.begin(9600);
    rfid_driver::init(&rfid1, &Serial1, TAG_TYPE_FDX, 100);

    // If you have a second port/reader:
    // Serial2.begin(9600);
    // rfid_driver_init(&rfid2, &Serial2, TAG_TYPE_HDX, 150);
}

void rfid_example_loop() {
    rfid_driver::tick(&rfid1);
    // rfid_driver::tick(&rfid2);

    tag_info_t t;
    if (rfid_driver::get_tag(&rfid1, &t)) {
        if (rfid_driver::should_record_tag(&prev1, &t, 2000)) {
            Serial.print("RFID1: ");
            Serial.println(t.tag);
            prev1 = t;
        }
    }
}
