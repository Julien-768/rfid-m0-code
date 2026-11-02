/**
 * @file assembly.h
 * @brief Declarations for managing system hardware/software identification (hw_assembly.cfg).
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
 * @struct Assembly
 * @brief Holds unique identifiers for all major hardware and software components.
 *
 * The Assembly structure encapsulates all persistent identifiers associated
 * with a device unit. Each field corresponds to a subsystem:
 * - Feather M0 UID (mainboard MCU)
 * - Sensor UIDs (spectral and light)
 * - Software and experiment identifiers
 * - Human-readable device serial number
 *
 * Example JSON representation:
 * @code{.json}
 * {
 *   "uid_mainboard": "FeatherM0_0x6F73XXXX",
 *   "uid_light_sensor1": "AS7341_0x24_0x08",
 *   "uid_light_sensor2": "TSL2591_0x50",
 *   "uid_software": "Moonraker_v0.1",
 *   "uid_experiment": "Moonraker",
 *   "device_sn": "RFD-0001"
 * }
 * @endcode
 */
struct Assembly {
    String uid_mainboard     = "$uid_mainboard$";      ///< Unique ID of the main board
    String uid_light_sensor1 = "$uid_light_sensor1$";  ///< Unique ID of the AS7341 spectral sensor.
    String uid_light_sensor2 = "$uid_light_sensor2$";  ///< Unique ID of the TSL2591 light sensor.
    String uid_software      = "$uid_software$";       ///< Software version or build identifier.
    String uid_experiment = "$uid_experiment$";  ///< Experiment or deployment context identifier.
    String device_sn = "$device_sn$";  ///< Human-readable device serial number (e.g., "RFD-0001").
    String battery_type = "$battery_type$";  ///< Battery type string (e.g., "lipo_1s", "liion_1s").
    String rtc_type     = "$rtc_type$";      ///< RTC type string (e.g., "ds3231", "none").

    /** @} */
};

/**
 * @brief Global instance of the @ref Assembly structure.
 *
 * Accessible throughout the project for reading or updating hardware/software metadata.
 */
extern Assembly hw_assembly;

/**
 * @brief Loads the hw_assembly configuration from the SD card.
 *
 * Opens and parses the JSON file `hw_assembly.cfg` located on the SD card,
 * and updates all fields of the provided @ref Assembly structure.
 * If the file is missing or corrupted, default values are retained
 * and a new configuration can be created later.
 *
 * @param hw_assembly Reference to the Assembly structure to populate.
 * @see assembly_save()
 */
bool assembly_load(Assembly& hw_assembly_local);

/**
 * @brief Saves the current Assembly configuration to the SD card.
 *
 * Serializes the provided @ref Assembly structure into JSON format
 * and writes it to the file `hw_assembly.cfg` on the SD card.
 * Existing files are overwritten to ensure data consistency.
 *
 * @param hw_assembly The Assembly instance to serialize and write.
 * @return true if the file was written successfully, false otherwise.
 * @see assembly_load()
 */
bool assembly_save(const Assembly& hw_assembly);

bool create_assembly_file();

/**
 * @brief Synchronize hw_assembly.cfg with factory identity stored in MCU flash.
 *
 * This helper reads the factory identity programmed in MCU non-volatile memory
 * (via @ref device_id_get()) and updates selected fields of the global
 * @ref Assembly instance (notably @ref Assembly::device_sn).
 *
 * If at least one field is modified, the function rewrites `hw_assembly.cfg`
 * using @ref assembly_save().
 *
 * Typical usage:
 * @code{.cpp}
 * assembly_load(hw_assembly);
 * device_id_initialization();
 * assembly_sync_sn();
 * @endcode
 *
 * @return true if hw_assembly.cfg was modified and saved, false otherwise.
 *
 * @see device_identity.h
 */
bool assembly_sync_sn(Assembly& hw_assembly);
