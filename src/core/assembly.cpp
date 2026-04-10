/**
 * @file hw_assembly.cpp
 * @defgroup Assembly_Manager Assembly Manager
 * @ingroup SystemModules
 * @brief SD-card persistent metadata for logger hardware/software identity (hw_assembly.cfg).
 *
 * This module manages the `hw_assembly.cfg` JSON file stored on the SD card.
 * It stores non-volatile metadata describing the logger unit:
 * - Mainboard UID (MCU / Feather UID)
 * - Sensor UIDs (AS7341, TSL2591)
 * - Software / experiment tags
 * - Human-readable logger serial number (SN)
 *
 * ## Relationship with factory identity (Flash)
 * The factory identity (manufacturer / type / date / serial number) is stored
 * in the SAMD21 non-volatile memory (Flash / NVM) via the `logger_identity` module.
 * At boot, this module can synchronize `hw_assembly.cfg` with that factory identity,
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
 * @warning SD card must be initialized (e.g., via sd_initialization()) before using any function
 *          in this module.
 *
 * @see assembly.h
 * @see logger_identity.h
 * @{
 */

#include "assembly.h"
#include <ArduinoJson.h>
#include <SD.h>
#include "log.h"
#include "logger_identity.h"

/**
 * @brief Path to the hw_assembly configuration file on the SD card.
 * maximum path length is 31 chars for 8.3 filename + null terminator, so this fits.
 */
constexpr const char* kAssemblyFilename = "/hw_assem.cfg";

/**
 * @brief Global instance of the hw_assembly metadata.
 *
 * This object is populated by @ref assembly_load() and may be updated at runtime.
 */
Assembly hw_assembly;

/**
 * @brief Create a new `hw_assembly.cfg` file on the SD card using current @ref hw_assembly values.
 *
 * Serializes the global @ref hw_assembly structure into a JSON document and writes it to
 * the SD card.
 * Intended for first boot or when re-initializing identity information.
 *
 * @note Requires SD card initialization before calling this function.
 * @note This function writes the current content of the global @ref hw_assembly. Ensure
 *       fields are set to meaningful defaults before calling.
 *
 * @see assembly_save()
 * @see assembly_load()
 */
void create_assembly_file() {
    File file = SD.open(kAssemblyFilename, FILE_WRITE);
    if (!file) {
        LOG_ERROR("Failed to create %s", kAssemblyFilename);
        return;
    }

    StaticJsonDocument<512> doc;
    doc["uid_mainboard"]     = hw_assembly.uid_mainboard;
    doc["uid_light_sensor1"] = hw_assembly.uid_light_sensor1;  // AS7341 sensor
    doc["uid_light_sensor2"] = hw_assembly.uid_light_sensor2;  // TSL2591 sensor
    doc["uid_software"]      = hw_assembly.uid_software;
    doc["uid_experiment"]    = hw_assembly.uid_experiment;
    doc["sn_logger"]         = hw_assembly.sn_logger;
    doc["battery_type"]      = hw_assembly.battery_type;
    doc["rtc_type"]          = hw_assembly.rtc_type;
    if (serializeJson(doc, file) == 0) {
        LOG_ERROR("Failed to write JSON to %s", kAssemblyFilename);
    } else {
        LOG_INFO("%s created successfully", kAssemblyFilename);
    }
    LOG_DEBUG("JSON capacity used: %d", doc.memoryUsage());
    file.close();
}

/**
 * @brief Load hw_assembly metadata from `hw_assembly.cfg` stored on the SD card.
 *
 * Attempts to open and parse the existing configuration file.
 * If parsing succeeds, updates the provided @p hw_assembly reference with loaded values.
 *
 * If the file does not exist, a new one is created using @ref create_assembly_file()
 * (based on the global @ref hw_assembly values).
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
 * @param hw_assembly Reference to an @ref Assembly struct to populate with values read from file.
 *
 * @note On JSON parse error, this function keeps existing values in @p hw_assembly.
 * @warning If the file is missing, @ref create_assembly_file() writes from the global
 *          @ref hw_assembly, not from the @p hw_assembly argument.
 *
 * @see create_assembly_file()
 * @see assembly_save()
 */
void assembly_load(Assembly& hw_assembly_local) {
    File file_c = SD.open(kAssemblyFilename);
    if (file_c) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, file_c);
        if (error) {
            LOG_ERROR("JSON parse error: %s", error.c_str());
        } else {
            Assembly tmp          = hw_assembly_local;
            tmp.uid_mainboard     = doc["uid_mainboard"] | String("$uid_mainboard$");
            tmp.uid_light_sensor1 = doc["uid_light_sensor1"] | String("$uid_light_sensor1$");
            tmp.uid_light_sensor2 = doc["uid_light_sensor2"] | String("$uid_light_sensor2$");
            tmp.uid_software      = doc["uid_software"] | String("$uid_software$");
            tmp.uid_experiment    = doc["uid_experiment"] | String("$uid_experiment$");
            tmp.sn_logger         = doc["sn_logger"] | String("$sn_logger$");
            tmp.battery_type      = doc["battery_type"] | String("$battery_type$");
            tmp.rtc_type          = doc["rtc_type"] | String("$rtc_type$");
            LOG_DEBUG("Assembly loaded from %s:", kAssemblyFilename);
            LOG_DEBUG("\tuid_mainboard: %s", tmp.uid_mainboard.c_str());
            LOG_DEBUG("\tuid_light_sensor1: %s", tmp.uid_light_sensor1.c_str());
            LOG_DEBUG("\tuid_light_sensor2: %s", tmp.uid_light_sensor2.c_str());
            LOG_DEBUG("\tuid_software: %s", tmp.uid_software.c_str());
            LOG_DEBUG("\tuid_experiment: %s", tmp.uid_experiment.c_str());
            LOG_DEBUG("\tsn_logger: %s", tmp.sn_logger.c_str());
            LOG_DEBUG("\tbattery_type: %s", tmp.battery_type.c_str());
            LOG_DEBUG("\trtc_type: %s", tmp.rtc_type.c_str());
            LOG_DEBUG("JSON capacity used: %d", doc.memoryUsage());
            delay(1000);
            hw_assembly_local = tmp;
            LOG_DEBUG("%s loaded successfully", kAssemblyFilename);
        }
        file_c.close();
    } else {
        LOG_WARN("%s not found — creating default file", kAssemblyFilename);
        create_assembly_file();
    }
}

/**
 * @brief Save an @ref Assembly instance to `hw_assembly.cfg` on the SD card.
 *
 * Rewrites the complete JSON file in pretty-printed format for readability.
 * The current file is removed then recreated to reduce the chance of leaving
 * partially updated content.
 *
 * @param hw_assembly The @ref Assembly instance to save.
 * @return true if the operation succeeds, false otherwise.
 *
 * @note This function deletes the existing file before writing a new one.
 *       If power is lost between remove and write, the file may be missing.
 * @warning SD card must be initialized before calling.
 *
 * @todo Consider writing to a temporary file then renaming for better atomicity
 *       (if filesystem constraints allow).
 *
 * @see assembly_load()
 * @see create_assembly_file()
 */
bool assembly_save(const Assembly& hw_assembly) {

    // Delete previous version if it exists
    if (SD.exists(kAssemblyFilename)) {
        SD.remove(kAssemblyFilename);
    }

    File file = SD.open(kAssemblyFilename, FILE_WRITE);
    if (!file) {
        LOG_ERROR("Failed to open %s for write", kAssemblyFilename);
        return false;
    }

    StaticJsonDocument<512> doc;
    doc["uid_mainboard"]     = hw_assembly.uid_mainboard;
    doc["uid_light_sensor1"] = hw_assembly.uid_light_sensor1;
    doc["uid_light_sensor2"] = hw_assembly.uid_light_sensor2;
    doc["uid_software"]      = hw_assembly.uid_software;
    doc["uid_experiment"]    = hw_assembly.uid_experiment;
    doc["sn_logger"]         = hw_assembly.sn_logger;
    doc["battery_type"]      = hw_assembly.battery_type;
    doc["rtc_type"]          = hw_assembly.rtc_type;
    LOG_DEBUG("JSON capacity used: %d", doc.memoryUsage());

    if (serializeJsonPretty(doc, file) == 0) {
        LOG_ERROR("Failed to write JSON to %s", kAssemblyFilename);
        file.close();
        return false;
    }

    file.close();
    LOG_INFO("%s saved successfully", kAssemblyFilename);
    return true;
}

/**
 * @brief Synchronize SD hw_assembly metadata with factory identity stored in MCU flash.
 *
 * Ensures that the SD card `hw_assembly.cfg` reflects the identity programmed in
 * SAMD21 flash (via @ref device_id_get()).
 *
 * Fields synchronized:
 * - `sn_logger` (only if the factory SN is not "UNKNOWN")
 *
 * The function rewrites `hw_assembly.cfg` only if at least one field changes.
 *
 * @return true if `hw_assembly.cfg` was modified and saved, false otherwise.
 *
 * @note This function updates the global @ref hw_assembly instance.
 * @warning This function performs SD writes and should not be called frequently.
 *
 * @see device_id_get()
 * @see assembly_save()
 */
bool assembly_sync_sn(Assembly& hw_assembly) {
    const LoggerIdentityFlash& id_flash = device_id_get();
    // bool modified                       = false;

    // --- Serial number sync ---
    if (strcmp(id_flash.serial_number, "UNKNOWN") != 0) {
        if (hw_assembly.sn_logger != id_flash.serial_number) {
            LOG_INFO("Updating SN from flash: %s -> %s", hw_assembly.sn_logger.c_str(),
                     id_flash.serial_number);
            hw_assembly.sn_logger = id_flash.serial_number;
            return true;
        }
    } else {
        LOG_WARN("Factory SN is UNKNOWN — hw_assembly SN unchanged");
        return false;
    }

    // // --- Save if anything changed ---
    // if (modified) {
    //     if (assembly_save(hw_assembly)) {
    //         LOG_INFO("hw_assembly.cfg synced with factory identity.");
    //         return true;
    //     } else {
    //         LOG_ERROR("Failed to save hw_assembly.cfg during identity sync.");
    //     }
    // }

    LOG_DEBUG("Assembly already up-to-date with factory identity.");
    return false;
}

/** @} */  // end of Assembly_Manager group
