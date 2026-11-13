/**
 * @file sensors_hal.cpp
 * @brief Generic Hardware Abstraction Layer (HAL) for sensor initialization
 *        and runtime acquisition.
 *
 * This module implements platform-agnostic helper routines used to:
 * - Initialize a list of sensors described by @ref SensorSpec.
 * - Read data from all active sensors into an application-defined context.
 *
 * The HAL remains completely generic: it does not depend on any code,
 * drivers, or data structures. All behaviors come from user-provided callbacks
 * in the SensorSpec table.
 *
 * Logging is performed using the LOG_* macros from log.h.
 *
 * @ingroup HalLayer
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

#include "sensors_hal.h"
#include "log.h"

/* ========================================================================== */
/*  Initialization Routine                                                    */
/* ========================================================================== */

/**
 * @brief Initialize all sensors declared in the provided SensorSpec table.
 *
 * Initialization logic:
 * - If `enabled` is null → entry is skipped.
 * - If `*enabled == false` → sensor is logged as disabled and skipped.
 * - If `initFn == nullptr` → considered a no-op (success).
 * - If `initFn()` returns false:
 *     - Logged as ERROR.
 *     - The configuration flag is set to false to prevent further use.
 * - On success → "`<name> initialized successfully`" is logged.
 *
 * This routine performs **initialization only**, not acquisition.
 *
 * @param specs Pointer to the first element of a SensorSpec array.
 * @param count Number of entries in the array.
 */
void SensorsHAL_InitForDeploy(SensorSpec* specs, size_t count) {
    LOG_DEBUG("Initializing sensors for Deploy mode...");

    for (size_t i = 0; i < count; ++i) {
        auto& s = specs[i];

        // ---- Disabled sensor ------------------------------------------------
        if (!(s.enabled && *s.enabled)) {
            LOG_INFO((String(s.name) + " disabled in configuration").c_str());
            continue;
        }

        // ---- No init function (optional sensors or virtual sensors) ---------
        if (!s.initFn) {
            LOG_WARN((String(s.name) + " has no init function, skipping init").c_str());
            continue;
        }

        // ---- Initialization failed -----------------------------------------
        if (!s.initFn()) {
            LOG_ERROR((String(s.name) + " initialization failed — disabled").c_str());
            *s.enabled = false;  // Prevent further acquisition
            continue;
        }

        // ---- Successful initialization --------------------------------------
        LOG_INFO((String(s.name) + " initialized successfully").c_str());
    }

    LOG_DEBUG("All active sensors initialized");
}

/* ========================================================================== */
/*  Acquisition Routine                                                       */
/* ========================================================================== */

/**
 * @brief Invoke acquisition callbacks for all active sensors.
 *
 * For each entry in @p specs:
 * - If `enabled == nullptr` or `*enabled == false` → skipped.
 * - If `readFn == nullptr` → skipped (sensor produces no runtime data).
 * - Otherwise → `readFn(context)` is called.
 *
 * The function does **not** allocate or manage the context object;
 * ownership is entirely the responsibility of the caller.
 *
 * @param specs   Sensor specification array.
 * @param count   Number of SensorSpec entries.
 * @param context Pointer to an application-defined structure (e.g. SensorFrame).
 */
void readAllSensorsGeneric(SensorSpec* specs, size_t count, void* context) {
    for (size_t i = 0; i < count; ++i) {
        auto& s = specs[i];

        if (!(s.enabled && *s.enabled)) continue;  ///< Sensor disabled
        if (!s.readFn) continue;                   ///< No runtime data

        s.readFn(context);
    }
}
