/**
 * @file JsonProtocol.cpp
 * @brief Implementation of JSON protocol helpers for the logger.
 *
 * Supported command formats (RX side):
 *
 * - Simple commands:
 *   @code
 *   { "command": "GET_INFO"   }
 *   { "command": "GET_ID"     }
 *   { "command": "GET_VBAT"   }
 *   { "command": "GET_CONFIG" }
 *   @endcode
 *
 * - Configuration command (SET_CONFIG):
 *   @code
 *   {
 *     "command": {
 *       "config": {
 *         "date_current": "2025-12-08T14:30:00",
 *         "acquisition_interval_s": 60,
 *         "enable_light1": true,
 *         "enable_light2": false,
 *         "enable_rfid": true,
 *         "rfid_mode": 2,
 *         "enable_vbat": true
 *       }
 *     }
 *   }
 *   @endcode
 *
 * - Factory identity command (SET_IDENTITY, factory tool only):
 *   @code
 *   {
 *     "command": {
 *       "identity": {
 *         "manufacturer": "CNRS",
 *         "logger_type":  "Moonraker",
 *         "date_fab":     "2025-01-01",
 *         "logger_sn":    "MRK-0001"
 *       }
 *     }
 *   }
 *   @endcode
 */

#include <ArduinoJson.h>
#include <string.h>  // strcmp, strncpy, memset, snprintf

#include "app/JsonProtocol.h"
#include "app/config.h"
#include "core/utils.h"

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------

namespace {
/**
 * @brief Format a small JSON error string.
 *
 * Writes a JSON object of the form:
 * @code
 * {"Error":"<msg>"}
 * @endcode
 *
 * @param buf Output buffer for the JSON string.
 * @param len Size of the output buffer in bytes.
 * @param msg Null-terminated error message string, may be nullptr.
 */
void writeErrorJson(char* buf, size_t len, const char* msg) {
    if (!buf || len == 0) return;

    snprintf(buf, len, "{\"Error\":\"%s\"}", msg ? msg : "Unknown error");
}
}  // namespace

// -----------------------------------------------------------------------------
// Parsing
// -----------------------------------------------------------------------------

/**
 * @brief Parse an incoming JSON command into a ParsedCommand structure.
 *
 * This function reads the root key `"command"` and supports two forms:
 *
 * 1. Simple string commands:
 *    - "GET_INFO"
 *    - "GET_ID"
 *    - "GET_VBAT"
 *    - "GET_CONFIG"
 *
 * 2. Object commands:
 *    - `{ "config": { ... } }`   -> SET_CONFIG
 *    - `{ "identity": { ... } }` -> SET_IDENTITY
 *
 * On success, @p out.type is set to the appropriate @ref CommandType and the
 * corresponding payload is filled when relevant.
 *
 * @param json_string Null-terminated JSON input string.
 * @param out Output structure with parsed command and payload.
 * @param errorBuf Output buffer for a small JSON error message.
 * @param errorBufLen Size of @p errorBuf in bytes.
 *
 * @return @c true if parsing succeeded and @p out.type != CommandType::NONE,
 *         @c false otherwise.
 */
bool JsonProtocol::parseCommand(const char* json_string, ParsedCommand& out, char* errorBuf,
                                size_t errorBufLen) {
    out.type = CommandType::NONE;

    if (!json_string) {
        writeErrorJson(errorBuf, errorBufLen, "Null JSON");
        return false;
    }

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, json_string);
    if (error) {
        writeErrorJson(errorBuf, errorBufLen, "Invalid JSON");
        return false;
    }

    // Root key "command" is required
    JsonVariant command = doc["command"];
    if (command.isNull()) {
        writeErrorJson(errorBuf, errorBufLen, "'command' is missing");
        return false;
    }

    // Case 1: "command" is a simple string
    if (command.is<const char*>()) {
        const char* cmd = command.as<const char*>();

        if (strcmp(cmd, "GET_INFO") == 0)
            out.type = CommandType::GET_INFO;
        else if (strcmp(cmd, "GET_ID") == 0)
            out.type = CommandType::GET_ID;
        else if (strcmp(cmd, "GET_VBAT") == 0)
            out.type = CommandType::GET_VBAT;
        else if (strcmp(cmd, "GET_CONFIG") == 0)
            out.type = CommandType::GET_CONFIG;
        else {
            writeErrorJson(errorBuf, errorBufLen, "Unknown command");
            return false;
        }

        if (errorBuf && errorBufLen) errorBuf[0] = '\0';
        return true;
    }

    // Case 2: "command" is an object
    if (command.is<JsonObject>()) {
        JsonObject cmdObj = command.as<JsonObject>();

        // SET_CONFIG
        if (cmdObj.containsKey("config")) {
            JsonObject jsonConfig = cmdObj["config"];

            if (!jsonConfig) {
                writeErrorJson(errorBuf, errorBufLen, "'config' is missing");
                return false;
            }

            const char* date_current = jsonConfig["date_current"];
            if (!date_current) {
                writeErrorJson(errorBuf, errorBufLen, "Missing 'date_current'");
                return false;
            }

            memset(&out.cfg, 0, sizeof(out.cfg));

            strncpy(out.cfg.dateCurrentIso, date_current, sizeof(out.cfg.dateCurrentIso) - 1);
            out.cfg.dateCurrentIso[sizeof(out.cfg.dateCurrentIso) - 1] = '\0';

            out.cfg.acquisition_interval_s =
                jsonConfig.containsKey("acquisition_interval_s")
                    ? jsonConfig["acquisition_interval_s"].as<uint16_t>()
                    : 0;

            out.cfg.enable_light1 = jsonConfig.containsKey("enable_light1")
                                        ? jsonConfig["enable_light1"].as<bool>()
                                        : false;

            out.cfg.enable_light2 = jsonConfig.containsKey("enable_light2")
                                        ? jsonConfig["enable_light2"].as<bool>()
                                        : false;

            out.cfg.enable_rfid = jsonConfig.containsKey("enable_rfid")
                                      ? jsonConfig["enable_rfid"].as<bool>()
                                      : false;

            out.cfg.rfid_mode =
                jsonConfig.containsKey("rfid_mode") ? jsonConfig["rfid_mode"].as<uint8_t>() : 0;

            out.cfg.enable_vbat = jsonConfig.containsKey("enable_vbat")
                                      ? jsonConfig["enable_vbat"].as<bool>()
                                      : false;

            out.type = CommandType::SET_CONFIG;
            if (errorBuf && errorBufLen) errorBuf[0] = '\0';
            return true;
        }

        // SET_IDENTITY
        if (cmdObj.containsKey("identity")) {
            JsonObject ident = cmdObj["identity"];

            const char* manufacturer = ident["manufacturer"] | "UNKNOWN";
            const char* logger_type  = ident["logger_type"] | "UNKNOWN";
            const char* date_fab     = ident["date_fab"] | "2025-01-01";
            const char* logger_sn    = ident["logger_sn"] | "UNKNOWN";

            memset(&out.identity, 0, sizeof(out.identity));

            strncpy(out.identity.manufacturer, manufacturer, sizeof(out.identity.manufacturer) - 1);
            strncpy(out.identity.logger_type, logger_type, sizeof(out.identity.logger_type) - 1);
            strncpy(out.identity.date_fab, date_fab, sizeof(out.identity.date_fab) - 1);
            strncpy(out.identity.logger_sn, logger_sn, sizeof(out.identity.logger_sn) - 1);

            out.type = CommandType::SET_IDENTITY;
            if (errorBuf && errorBufLen) errorBuf[0] = '\0';
            return true;
        }

        writeErrorJson(errorBuf, errorBufLen, "Unsupported 'command' object");
        return false;
    }

    writeErrorJson(errorBuf, errorBufLen, "Unsupported 'command' format");
    return false;
}

// -----------------------------------------------------------------------------
// JSON builders
// -----------------------------------------------------------------------------

/**
 * @brief Build JSON with firmware version and compilation date.
 *
 * Uses @ref convertDateToISO8601() to generate the compilation timestamp.
 *
 * @param version Firmware version string.
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* JsonProtocol::buildInfoJSON(const char* version) {
    static char buffer[128];
    StaticJsonDocument<128> doc;

    char dateCompil[23];
    convertDateToISO8601(dateCompil, sizeof(dateCompil));

    JsonObject obj          = doc.createNestedObject("info");
    obj["version"]          = version ? version : "TODO";
    obj["compilation_date"] = dateCompil;

    serializeJson(doc, buffer);
    return buffer;
}

/**
 * @brief Build JSON with logger identification details.
 *
 * @param payload Reference to a @ref SetIdentityPayload structure.
 * @return Pointer to a static internal buffer containing the serialized JSON.
 */
const char* JsonProtocol::buildIdJSON(const SetIdentityPayload& payload) {
    static char buffer[192];
    StaticJsonDocument<192> doc;

    JsonObject obj      = doc.createNestedObject("id");
    obj["uid_mcu"]      = payload.UID[0] ? payload.UID : "UNKNOWN";
    obj["manufacturer"] = payload.manufacturer[0] ? payload.manufacturer : "UNKNOWN";
    obj["date_fab"]     = payload.date_fab[0] ? payload.date_fab : "";
    obj["logger_type"]  = payload.logger_type[0] ? payload.logger_type : "UNKNOWN";
    obj["logger_sn"]    = payload.logger_sn[0] ? payload.logger_sn : "";

    serializeJson(doc, buffer);
    return buffer;
}

/**
 * @brief Build JSON with battery voltage in millivolts.
 *
 * @param voltage_mV Battery voltage in millivolts.
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* JsonProtocol::buildVbatJSON(unsigned int voltage_mV) {
    static char buffer[64];
    StaticJsonDocument<64> doc;

    JsonObject obj = doc.createNestedObject("vbat");
    obj["vbat_mV"] = voltage_mV;

    serializeJson(doc, buffer);
    return buffer;
}

/**
 * @brief Build a JSON snapshot of the current configuration.
 *
 * Uses:
 * - @c gui_time_sync_rb.dateCurrent as the current date/time
 * - @c config as the runtime configuration
 *
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* JsonProtocol::buildConfigJSON() {
    static char buffer[256];
    StaticJsonDocument<256> doc;

    char dateCurrentStr[23];
    convertBcdDateToISO8601(&gui_time_sync_rb.dateCurrent, dateCurrentStr, sizeof(dateCurrentStr));

    JsonObject obj                = doc.createNestedObject("config");
    obj["date_current"]           = dateCurrentStr;
    obj["acquisition_interval_s"] = config.acquisition_interval_s;
    obj["enable_light1"]          = config.enable_light1;
    obj["enable_light2"]          = config.enable_light2;
    obj["enable_rfid"]            = config.enable_rfid;
    obj["rfid_mode"]              = config.rfid_mode;
    obj["enable_vbat"]            = config.enable_vbat;

    serializeJson(doc, buffer);
    return buffer;
}
