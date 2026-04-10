/**
 * @file assembly.h
 * @brief Declarations for managing system hardware/software identification (hw_assembly.cfg).
 *
 * This header defines the @ref Assembly structure and related functions used
 * to manage persistent metadata describing the logger system.
 *
 * ## Overview
 * The Assembly structure centralizes unique identifiers (UIDs) for:
 * - Mainboard (Feather M0 microcontroller)
 * - Light sensors (AS7341 and TSL2591)
 * - Software and experiment identifiers
 * - Logger serial number (SN)
 *
 * These fields are serialized into and read from the `hw_assembly.cfg` file
 * stored on the SD card. This ensures reproducibility, traceability, and
 * version control for deployed systems.
 *
 * @see hw_assembly.cpp
 * @see logger_identity.h
 */

#pragma once
#include <Arduino.h>

/**
 * @struct Assembly
 * @brief Holds unique identifiers for all major hardware and software components.
 *
 * The Assembly structure encapsulates all persistent identifiers associated
 * with a logger unit. Each field corresponds to a subsystem:
 * - Feather M0 UID (mainboard MCU)
 * - Sensor UIDs (spectral and light)
 * - Software and experiment identifiers
 * - Human-readable logger serial number
 *
 * Example JSON representation:
 * @code{.json}
 * {
 *   "uid_mainboard": "FeatherM0_0x6F73XXXX",
 *   "uid_light_sensor1": "AS7341_0x24_0x08",
 *   "uid_light_sensor2": "TSL2591_0x50",
 *   "uid_software": "Moonraker_v0.1",
 *   "uid_experiment": "Moonraker",
 *   "sn_logger": "MRK-0001"
 * }
 * @endcode
 */
struct Assembly {
    String uid_mainboard     = "$uid_mainboard$";      ///< Unique ID of the main board
    String uid_light_sensor1 = "$uid_light_sensor1$";  ///< Unique ID of the AS7341 spectral sensor.
    String uid_light_sensor2 = "$uid_light_sensor2$";  ///< Unique ID of the TSL2591 light sensor.
    String uid_software      = "$uid_software$";       ///< Software version or build identifier.
    String uid_experiment = "$uid_experiment$";  ///< Experiment or deployment context identifier.
    String sn_logger = "$sn_logger$";  ///< Human-readable logger serial number (e.g., "MRK-0007").
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
void assembly_load(Assembly& hw_assembly_local);

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

void create_assembly_file();

/**
 * @brief Synchronize hw_assembly.cfg with factory identity stored in MCU flash.
 *
 * This helper reads the factory identity programmed in MCU non-volatile memory
 * (via @ref device_id_get()) and updates selected fields of the global
 * @ref Assembly instance (notably @ref Assembly::sn_logger).
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
 * @see logger_identity.h
 */
bool assembly_sync_sn(Assembly& hw_assembly);
