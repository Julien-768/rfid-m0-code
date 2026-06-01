/**
 * @file Config.cpp
 * @brief Runtime configuration handling for the logger.
 *
 * This module defines the global runtime configuration and provides helpers
 * to load options from the SD card.
 *
 * It intentionally does not depend on JsonProtocol.
 *
 * @note JSON parsing uses ArduinoJson v6.
 * @warning SD card must be initialized before calling @ref load_configuration().
 */

#include <ArduinoJson.h>
#include <SD.h>

#include "config.h"
#include "log.h"

Config config;

static const char* boolStr(bool value) {
    return value ? "true" : "false";
}

static bool isScheduleValid(const Config& config_var) {
    return config_var.schedule_start_hour <= 23 && config_var.schedule_start_minute <= 59 &&
           config_var.schedule_end_hour <= 23 && config_var.schedule_end_minute <= 59;
}

static void logConfiguration(const Config& config_var, const char* title) {
    LOG_INFO("%s", title);
    LOG_INFO("\tuse_buffer: %s", boolStr(config_var.use_buffer));
    LOG_INFO("\tlight1: %s", boolStr(config_var.enable_ir1));
    LOG_INFO("\tlight2: %s", boolStr(config_var.enable_ir2));
    LOG_INFO("\trfid: %s", boolStr(config_var.enable_rfid));
    LOG_INFO("\trfid_mode: %u", rfidModeToUint(config_var.rfid_mode));
    LOG_INFO("\tvbat: %s", boolStr(config_var.enable_vbat));
    LOG_INFO("\tinterval: %u s", config_var.acquisition_interval_s);
    LOG_INFO("\tschedule: %02u:%02u -> %02u:%02u",
             config_var.schedule_start_hour,
             config_var.schedule_start_minute,
             config_var.schedule_end_hour,
             config_var.schedule_end_minute);
}

static void readBoolField(const JsonDocument& doc, const char* key, bool& destination) {
    if (!doc.containsKey(key)) return;

    if (!doc[key].is<bool>()) {
        LOG_WARN("Invalid boolean value for '%s'. Keeping previous value.", key);
        return;
    }

    destination = doc[key].as<bool>();
}

static void readUint8Field(const JsonDocument& doc, const char* key, uint8_t& destination) {
    if (!doc.containsKey(key)) return;

    if (!doc[key].is<uint8_t>()) {
        LOG_WARN("Invalid uint8 value for '%s'. Keeping previous value.", key);
        return;
    }

    destination = doc[key].as<uint8_t>();
}

static void readIntervalField(const JsonDocument& doc, const char* key, uint16_t& destination) {
    if (!doc.containsKey(key)) return;

    if (!doc[key].is<uint16_t>()) {
        LOG_WARN("Invalid numeric value for '%s'. Keeping previous value.", key);
        return;
    }

    const uint16_t raw_value = doc[key].as<uint16_t>();

    if (raw_value < MIN_ACQUISITION_INTERVAL_S) {
        LOG_WARN("Invalid acquisition interval %u s. Keeping previous value.", raw_value);
        return;
    }

    destination = raw_value;
}

static void readRfidModeField(const JsonDocument& doc, const char* key, RfidMode& destination) {
    if (!doc.containsKey(key)) return;

    if (!doc[key].is<uint8_t>()) {
        LOG_WARN("Invalid RFID mode value for '%s'. Keeping previous value.", key);
        return;
    }

    const uint8_t raw_value = doc[key].as<uint8_t>();

    if (raw_value > rfidModeToUint(RfidMode::OnIrEvent)) {
        LOG_WARN("Invalid RFID mode %u. Keeping previous value.", raw_value);
        return;
    }

    destination = rfidModeFromUint(raw_value);
}

uint8_t rfidModeToUint(RfidMode mode) {
    return static_cast<uint8_t>(mode);
}

RfidMode rfidModeFromUint(uint8_t value) {
    switch (value) {
        case 0:
            return RfidMode::Off;
        case 1:
            return RfidMode::Continuous;
        case 2:
            return RfidMode::OnIrEvent;
        default:
            return DEFAULT_RFID_MODE;
    }
}

/**
 * @brief Validate and sanitize runtime configuration.
 */
bool validateConfig(Config& config_var) {
    bool valid = true;

    if (config_var.acquisition_interval_s < MIN_ACQUISITION_INTERVAL_S) {
        LOG_WARN("Invalid acquisition interval %u s. Falling back to %u s.",
                 config_var.acquisition_interval_s,
                 DEFAULT_ACQUISITION_INTERVAL_S);
        config_var.acquisition_interval_s = DEFAULT_ACQUISITION_INTERVAL_S;
        valid                             = false;
    }

    if (!isScheduleValid(config_var)) {
        LOG_WARN("Invalid schedule. Falling back to default schedule 08:00 -> 18:30.");

        config_var.schedule_start_hour   = DEFAULT_SCHEDULE_START_HOUR;
        config_var.schedule_start_minute = DEFAULT_SCHEDULE_START_MINUTE;
        config_var.schedule_end_hour     = DEFAULT_SCHEDULE_END_HOUR;
        config_var.schedule_end_minute   = DEFAULT_SCHEDULE_END_MINUTE;

        valid = false;
    }

    return valid;
}

/**
 * @brief Load runtime configuration from `/config.cfg` on the SD card.
 */
bool load_configuration(Config& config_var) {
    File file = SD.open("/config.cfg");

    if (!file) {
        LOG_WARN("config.cfg not found on SD card. Using defaults.");
        validateConfig(config_var);
        logConfiguration(config_var, "Default configuration:");
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        LOG_ERROR("Failed to parse config.cfg: %s", error.c_str());
        validateConfig(config_var);
        logConfiguration(config_var, "Configuration after parse failure:");
        return false;
    }

    readBoolField(doc, "use_buffer", config_var.use_buffer);
    readBoolField(doc, "enable_ir1", config_var.enable_ir1);
    readBoolField(doc, "enable_ir2", config_var.enable_ir2);
    readBoolField(doc, "enable_rfid", config_var.enable_rfid);
    readRfidModeField(doc, "rfid_mode", config_var.rfid_mode);
    readBoolField(doc, "enable_vbat", config_var.enable_vbat);
    readIntervalField(doc, "acquisition_interval_s", config_var.acquisition_interval_s);

    readUint8Field(doc, "schedule_start_hour", config_var.schedule_start_hour);
    readUint8Field(doc, "schedule_start_minute", config_var.schedule_start_minute);
    readUint8Field(doc, "schedule_end_hour", config_var.schedule_end_hour);
    readUint8Field(doc, "schedule_end_minute", config_var.schedule_end_minute);

    validateConfig(config_var);
    logConfiguration(config_var, "Configuration loaded:");

    return true;
}
