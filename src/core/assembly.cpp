/**
 * @file hw_assembly.cpp
 * @defgroup Assembly_Manager Assembly Manager
 * @ingroup SystemModules
 * @brief SD-card persistent metadata for device hardware/software identity (hw_assembly.cfg).
 *
 * This module manages the `hw_assembly.cfg` JSON file stored on the SD card.
 * It stores non-volatile metadata describing the device unit:
 * - Mainboard UID (MCU / Feather UID)
 * - Sensor UIDs (AS7341, TSL2591)
 * - Software / experiment tags
 * - Human-readable device serial number (SN)
 *
 * ## Relationship with factory identity (Flash)
 * The factory identity (manufacturer / type / date / serial number) is stored
 * in the SAMD21 non-volatile memory (Flash / NVM) via the `device_identity` module.
 * At boot, this module can synchronize `hw_assembly.cfg` with that factory identity,
 * mainly to ensure `device_sn` matches the factory-programmed serial number.
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
 *   "device_sn": "RFD-0001"
 * }
 * @endcode
 *
 * @note JSON I/O is handled via ArduinoJson.
 * @warning SD card must be initialized (e.g., via sd_initialization()) before using any function
 *          in this module.
 *
 * @see assembly.h
 * @see device_identity.h
 * @{
 */

#include "assembly.h"
#include <ArduinoJson.h>
#include <SD.h>
#include "log.h"
#include "device_identity.h"

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

static bool is_placeholder(const String& value) {
    return value.startsWith("$") && value.endsWith("$");
}

static uint8_t assembly_count_unprovisioned_fields(const Assembly& a) {
    uint8_t count = 0;

    if (is_placeholder(a.uid_light_sensor1)) count++;
    if (is_placeholder(a.uid_light_sensor2)) count++;
    if (is_placeholder(a.uid_software)) count++;
    if (is_placeholder(a.uid_experiment)) count++;
    if (is_placeholder(a.battery_type)) count++;

    return count;
}

static void assembly_log_unprovisioned_fields(const Assembly& a) {
    uint8_t count = assembly_count_unprovisioned_fields(a);

    if (count == 0) {
        return;
    }

    LOG_WARN("Assembly config incomplete: %u unprovisioned field(s) remaining", count);

    if (is_placeholder(a.uid_light_sensor1)) {
        LOG_WARN("Unprovisioned: uid_light_sensor1");
    }
    if (is_placeholder(a.uid_light_sensor2)) {
        LOG_WARN("Unprovisioned: uid_light_sensor2");
    }
    if (is_placeholder(a.uid_software)) {
        LOG_WARN("Unprovisioned: uid_software");
    }
    if (is_placeholder(a.uid_experiment)) {
        LOG_WARN("Unprovisioned: uid_experiment");
    }
    if (is_placeholder(a.battery_type)) {
        LOG_WARN("Unprovisioned: battery_type");
    }
}

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
bool create_assembly_file() {
    File file = SD.open(kAssemblyFilename, FILE_WRITE);
    if (!file) {
        LOG_ERROR("Failed to create %s", kAssemblyFilename);
        return false;
    }

    StaticJsonDocument<512> doc;
    doc["uid_mainboard"]     = hw_assembly.uid_mainboard;
    doc["uid_light_sensor1"] = hw_assembly.uid_light_sensor1;  // AS7341 sensor
    doc["uid_light_sensor2"] = hw_assembly.uid_light_sensor2;  // TSL2591 sensor
    doc["uid_software"]      = hw_assembly.uid_software;
    doc["uid_experiment"]    = hw_assembly.uid_experiment;
    doc["device_sn"]         = hw_assembly.device_sn;
    doc["battery_type"]      = hw_assembly.battery_type;
    doc["rtc_type"]          = hw_assembly.rtc_type;
    LOG_DEBUG("JSON capacity used: %d", doc.memoryUsage());
    if (serializeJson(doc, file) == 0) {
        LOG_ERROR("Failed to write JSON to %s", kAssemblyFilename);
        file.close();
        return false;
    } else {
        LOG_INFO("%s created successfully", kAssemblyFilename);
        file.close();
        return true;
    }
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
 *   "device_sn":        "RFD-0001"
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
bool assembly_load(Assembly& hw_assembly_local) {
    if (!SD.exists(kAssemblyFilename)) {
        LOG_ERROR("%s not found", kAssemblyFilename);

        return false;
    }

    File file_c = SD.open(kAssemblyFilename, FILE_READ);
    if (!file_c) {
        LOG_ERROR("Failed to open %s", kAssemblyFilename);
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file_c);
    file_c.close();

    LOG_DEBUG("JSON capacity used: %d", doc.memoryUsage());

    if (error) {
        LOG_ERROR("JSON parse error in %s: %s", kAssemblyFilename, error.c_str());
        return false;
    }

    const char* required[] = {"uid_mainboard",
                              "uid_light_sensor1",
                              "uid_light_sensor2",
                              "uid_software",
                              "uid_experiment",
                              "device_sn",
                              "battery_type",
                              "rtc_type"};

    for (const char* key : required) {
        if (!doc.containsKey(key)) {
            LOG_ERROR("Missing key in %s: %s", kAssemblyFilename, key);
            return false;
        }
    }

    hw_assembly_local.uid_mainboard     = doc["uid_mainboard"].as<String>();
    hw_assembly_local.uid_light_sensor1 = doc["uid_light_sensor1"].as<String>();
    hw_assembly_local.uid_light_sensor2 = doc["uid_light_sensor2"].as<String>();
    hw_assembly_local.uid_software      = doc["uid_software"].as<String>();
    hw_assembly_local.uid_experiment    = doc["uid_experiment"].as<String>();
    hw_assembly_local.device_sn         = doc["device_sn"].as<String>();
    hw_assembly_local.battery_type      = doc["battery_type"].as<String>();
    hw_assembly_local.rtc_type          = doc["rtc_type"].as<String>();

    assembly_log_unprovisioned_fields(hw_assembly_local);

    LOG_INFO("%s loaded successfully", kAssemblyFilename);
    return true;
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
    doc["device_sn"]         = hw_assembly.device_sn;
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
 * - `device_sn` (only if the factory SN is not "UNKNOWN")
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
    const deviceIdentityFlash& id_flash = device_id_get();
    // bool modified                       = false;

    // --- Serial number sync ---
    if (strcmp(id_flash.serial_number, "UNKNOWN") != 0) {
        if (hw_assembly.device_sn != id_flash.serial_number) {
            LOG_INFO("Updating SN from flash: %s -> %s",
                     hw_assembly.device_sn.c_str(),
                     id_flash.serial_number);
            hw_assembly.device_sn = id_flash.serial_number;
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
