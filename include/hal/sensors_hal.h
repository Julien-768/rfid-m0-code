/**
 * @file sensors_hal.h
 * @brief Generic Hardware Abstraction Layer (HAL) for sensor initialization
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
 * @brief One generic sensor entry handled by the HAL.
 *
 * Each entry defines:
 * - @c name    : Human-readable name used for logging.
 * - @c enabled : Pointer to the configuration flag that activates the sensor.
 * - @c initFn  : Initialization callback (may be @c nullptr).
 * - @c readFn  : Acquisition callback writing results into the provided context
 *                (may be @c nullptr for sensors with no runtime data).
 *
 * The application defines the "context" structure (typically @c SensorFrame).
 * SensorSpec remains intentionally minimal so it can be reused in any project.
 */
struct SensorSpec {
    const char* name;               ///< Display name used in logs.
    bool* enabled;                  ///< Pointer to activation flag (may be nullptr).
    bool (*initFn)();               ///< Initialization callback. Returns true on success.
    void (*readFn)(void* context);  ///< Runtime read callback writing into @p context.
};

/**
 * @brief Initialize all sensors marked as enabled in the given table.
 *
 * For each entry:
 * - If @c enabled is nullptr → the entry is skipped.
 * - If @c *enabled == false → logged as disabled, skipped.
 * - If @c *enabled == true:
 *     - If @c initFn is nullptr → considered a no-op and treated as success.
 *     - If @c initFn fails → logged as ERROR and @c *enabled is set to false.
 *     - On success → logs "<sensor> initialized successfully".
 *
 * This function performs *only* initialization, not acquisition.
 *
 * @param table Pointer to the first element of a SensorSpec array.
 * @param count Number of elements in @p table.
 */
void SensorsHAL_InitForDeploy(SensorSpec* table, size_t count);

/**
 * @brief Read all active sensors into an application-defined context object.
 *
 * For each enabled sensor:
 * - If @c readFn is not null → invoked with @p context.
 * - If @c readFn is null → the sensor simply produces no runtime data.
 *
 * The structure pointed to by @p context is fully application-defined
 * (e.g., a @c SensorFrame struct).
 *
 * @param specs   Sensor specification array.
 * @param count   Number of entries in @p specs.
 * @param context Opaque pointer passed to each @c readFn callback.
 */
void readAllSensorsGeneric(SensorSpec* specs, size_t count, void* context);
