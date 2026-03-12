/**
 * @file assembly.cpp
 * @defgroup Assembly_Manager Assembly Manager
 * @ingroup SystemModules
 * @brief SD-card persistent metadata for logger hardware/software identity (assembly.cfg).
 *
 * This module manages the `assembly.cfg` JSON file stored on the SD card.
 * It stores non-volatile metadata describing the logger unit:
 * - Mainboard UID (MCU / Feather UID)
 * - Sensor UIDs (AS7341, TSL2591)
 * - Software / experiment tags
 * - Human-readable logger serial number (SN)
 *
 * ## Relationship with factory identity (Flash)
 * The factory identity (manufacturer / type / date / serial number) is stored
 * in the SAMD21 non-volatile memory (Flash / NVM) via the `logger_identity` module.
 * At boot, this module can synchronize `assembly.cfg` with that factory identity,
 * mainly to ensure `sn_logger` matches the factory-programmed serial number.
 *
 * ## File format
 * Example JSON:
 * @code{.json}
 * {
 *   "uid_mainboard": "FeatherM0_0x6F73XXXX",
 *   "uid_light_sensor1": "AS7341_0x24_0x08",
 *   "uid_light_sensor2": "TSL2591_0x50",
 *   "uid_software": "Moonraker_v1.0.0",
 *   "uid_experiment": "Moonraker",
 *   "sn_logger": "MRK-0001"
 * }
 * @endcode
 *
 * @note JSON I/O is handled via ArduinoJson.
 * @warning SD card must be initialized (e.g., via initSD()) before using any function
 *          in this module.
 *
 * @see assembly.h
 * @see logger_identity.h
 * @{
 */

#include <SD.h>
#include <ArduinoJson.h>
#include "core/assembly.h"
#include "core/log.h"
#include "app/logger_identity.h"

/**
 * @brief Global instance of the assembly metadata.
 *
 * This object is populated by @ref loadAssembly() and may be updated at runtime.
 */
Assembly assembly;

/**
 * @brief Path to the assembly configuration file on the SD card.
 */
const char* filename_assembly = "/assembly.cfg";

/**
 * @brief Create a new `assembly.cfg` file on the SD card using current @ref assembly values.
 *
 * Serializes the global @ref assembly structure into a JSON document and writes it to
 * the SD card.
 * Intended for first boot or when re-initializing identity information.
 *
 * @note Requires SD card initialization before calling this function.
 * @note This function writes the current content of the global @ref assembly. Ensure
 *       fields are set to meaningful defaults before calling.
 *
 * @see saveAssembly()
 * @see loadAssembly()
 */
void create_assembly_file() {
    File file = SD.open(filename_assembly, FILE_WRITE);
    if (!file)
        {
            LOG_ERROR("Failed to create assembly.cfg");
            return;
    }

    StaticJsonDocument<512> doc;
    doc["uid_mainboard"]     = assembly.uid_mainboard;
    doc["uid_light_sensor1"] = assembly.uid_light_sensor1;  // AS7341 sensor
    doc["uid_light_sensor2"] = assembly.uid_light_sensor2;  // TSL2591 sensor
    doc["uid_software"]      = assembly.uid_software;
    doc["uid_experiment"]    = assembly.uid_experiment;
    doc["sn_logger"]         = assembly.sn_logger;

    if (serializeJson(doc, file) == 0)
        {
            LOG_ERROR("Failed to write JSON to assembly.cfg");
    } else
        {
            LOG_INFO("assembly.cfg created successfully");
        }

    file.close();
}

/**
 * @brief Load assembly metadata from `assembly.cfg` stored on the SD card.
 *
 * Attempts to open and parse the existing configuration file.
 * If parsing succeeds, updates the provided @p assembly reference with loaded values.
 *
 * If the file does not exist, a new one is created using @ref create_assembly_file()
 * (based on the global @ref assembly values).
 *
 * @code{.json}
 * {
 *   "uid_mainboard":    "FeatherM0_001",
 *   "uid_light_sensor1":"AS7341_0x24_0x08",
 *   "uid_light_sensor2":"TSL2591_0x50",
 *   "uid_software":     "Moonraker_v0.1",
 *   "uid_experiment":   "Moonraker",
 *   "sn_logger":        "MRK-0001"
 * }
 * @endcode
 *
 * @param assembly Reference to an @ref Assembly struct to populate with values read from file.
 *
 * @note On JSON parse error, this function keeps existing values in @p assembly.
 * @warning If the file is missing, @ref create_assembly_file() writes from the global
 *          @ref assembly, not from the @p assembly argument.
 *
 * @see create_assembly_file()
 * @see saveAssembly()
 */
void loadAssembly(Assembly& assembly) {
    File file_c = SD.open(filename_assembly);
    if (file_c)
        {
            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, file_c);
            if (error)
                {
                    LOG_ERROR("Failed to read assembly.cfg — keeping defaults");
            } else
                {
                    assembly.uid_mainboard     = doc["uid_mainboard"].as<String>();
                    assembly.uid_light_sensor1 = doc["uid_light_sensor1"].as<String>();
                    assembly.uid_light_sensor2 = doc["uid_light_sensor2"].as<String>();
                    assembly.uid_software      = doc["uid_software"].as<String>();
                    assembly.uid_experiment    = doc["uid_experiment"].as<String>();
                    assembly.sn_logger         = doc["sn_logger"] | String("");
                }
            file_c.close();
    } else
        {
            LOG_WARN("assembly.cfg not found — creating default file");
            create_assembly_file();
        }
}

/**
 * @brief Save an @ref Assembly instance to `assembly.cfg` on the SD card.
 *
 * Rewrites the complete JSON file in pretty-printed format for readability.
 * The current file is removed then recreated to reduce the chance of leaving
 * partially updated content.
 *
 * @param assembly The @ref Assembly instance to save.
 * @return true if the operation succeeds, false otherwise.
 *
 * @note This function deletes the existing file before writing a new one.
 *       If power is lost between remove and write, the file may be missing.
 * @warning SD card must be initialized before calling.
 *
 * @todo Consider writing to a temporary file then renaming for better atomicity
 *       (if filesystem constraints allow).
 *
 * @see loadAssembly()
 * @see create_assembly_file()
 */
bool saveAssembly(const Assembly& assembly) {
    const char* filename_assembly = "/assembly.cfg";  // local shadowing of global

    // Delete previous version if it exists
    if (SD.exists(filename_assembly))
        {
            SD.remove(filename_assembly);
    }

    File file = SD.open(filename_assembly, FILE_WRITE);
    if (!file)
        {
            LOG_ERROR("Failed to open assembly.cfg for write");
            return false;
    }

    StaticJsonDocument<512> doc;
    doc["uid_mainboard"]     = assembly.uid_mainboard;
    doc["uid_light_sensor1"] = assembly.uid_light_sensor1;
    doc["uid_light_sensor2"] = assembly.uid_light_sensor2;
    doc["uid_software"]      = assembly.uid_software;
    doc["uid_experiment"]    = assembly.uid_experiment;
    doc["sn_logger"]         = assembly.sn_logger;

    if (serializeJsonPretty(doc, file) == 0)
        {
            LOG_ERROR("Failed to write JSON to assembly.cfg");
            file.close();
            return false;
    }

    file.close();
    LOG_INFO("assembly.cfg saved successfully");
    return true;
}

/**
 * @brief Synchronize SD assembly metadata with factory identity stored in MCU flash.
 *
 * Ensures that the SD card `assembly.cfg` reflects the identity programmed in
 * SAMD21 flash (via @ref loggerIdentity_get()).
 *
 * Fields synchronized:
 * - `sn_logger` (only if the factory SN is not "UNKNOWN")
 * - `uid_software` (set to a default if empty)
 * - `uid_experiment` (set to a default if empty)
 *
 * The function rewrites `assembly.cfg` only if at least one field changes.
 *
 * @return true if `assembly.cfg` was modified and saved, false otherwise.
 *
 * @note This function updates the global @ref assembly instance.
 * @warning This function performs SD writes and should not be called frequently.
 *
 * @see loggerIdentity_get()
 * @see saveAssembly()
 */
bool syncAssemblyWithFactoryIdentity() {
    const LoggerIdentityFlash& idFlash = loggerIdentity_get();
    bool modified                      = false;

    // --- Serial number sync ---
    if (strcmp(idFlash.serial_number, "UNKNOWN") != 0)
        {
            if (assembly.sn_logger != idFlash.serial_number)
                {
                    LOG_INFO("Updating SN from flash: %s -> %s", assembly.sn_logger.c_str(), idFlash.serial_number);
                    assembly.sn_logger = idFlash.serial_number;
                    modified           = true;
            }
    } else
        {
            LOG_WARN("Factory SN is UNKNOWN — assembly SN unchanged");
        }

    // --- Optional: defaults for experiment and software ---
    if (assembly.uid_software.length() == 0)
        {
            assembly.uid_software = "Moonraker_v1.0.0";
            modified              = true;
    }

    if (assembly.uid_experiment.length() == 0)
        {
            assembly.uid_experiment = "Moonraker";
            modified                = true;
    }

    // --- Save if anything changed ---
    if (modified)
        {
            if (saveAssembly(assembly))
                {
                    LOG_INFO("assembly.cfg synced with factory identity.");
                    return true;
            } else
                {
                    LOG_ERROR("Failed to save assembly.cfg during identity sync.");
                }
    }

    LOG_DEBUG("Assembly already up-to-date with factory identity.");
    return false;
}

/** @} */  // end of Assembly_Manager group
