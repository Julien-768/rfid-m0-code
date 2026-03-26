/**
 * @file JsonProtocol.cpp
 * @brief Implementation of JSON protocol helpers for Moonraker.
 *
 * Supported command formats (RX side):
 *
 *  - Simple commands:
 *    @code
 *    { "command": "GET_INFO"   }
 *    { "command": "GET_ID"     }
 *    { "command": "GET_VBAT"   }
 *    { "command": "GET_CONFIG" }
 *    @endcode
 *
 *  - Configuration command (SET_CONFIG):
 *    @code
 *    {
 *      "command": {
 *        "config": {
 *          "date_current": "2025-12-08T14:30:00",
 *          "acquisition_interval_s": 60,
 *          "enable_light1": true,
 *          "enable_light2": false,
 *          "enable_vbat": true
 *        }
 *      }
 *    }
 *    @endcode
 *
 *  - Factory identity command (SET_IDENTITY, factory tool only):
 *    @code
 *    {
 *      "command": {
 *        "identity": {
 *          "manufacturer": "CNRS",
 *          "logger_type":  "Moonraker",
 *          "date_fab":     "2025-01-01",
 *          "logger_sn":    "MRK-0001"
 *        }
 *      }
 *    }
 *    @endcode
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
 * @brief Helper to format a small JSON error string.
 *
 * Writes a JSON object of the form:
 * @code
 * {"Error":"<msg>"}
 * @endcode
 *
 * @param buf Output buffer for the JSON string.
 * @param len Size of the output buffer in bytes.
 * @param msg Null-terminated error message string (may be nullptr).
 */
void writeErrorJson(char* buf, size_t len, const char* msg) {
    if (!buf || len == 0) return;

    // Simple format: {"Error":"..."}
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
 *    - `{ "config":   { ... } }`  → SET_CONFIG
 *    - `{ "identity": { ... } }` → SET_IDENTITY (factory-only)
 *
 * On success, @p out.type is set to the appropriate @ref CommandType and the
 * corresponding payload (cfg / identity) is filled if relevant.
 *
 * @param json_string Null-terminated JSON input string.
 * @param out         Output structure with parsed command and payload.
 * @param errorBuf    Output buffer for a small JSON error message
 *                    (e.g. {"Error":"...'command' is missing"}) or empty string.
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

    // --- CASE 1 : "command" = "GET_XXX" ---
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

    // --- CASE 2 : "command" is an object → SET_CONFIG or SET_IDENTITY ---
    if (command.is<JsonObject>()) {
        JsonObject cmdObj = command.as<JsonObject>();

        // a) SET_CONFIG
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

            // Reset payload and copy fields
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

            out.cfg.enable_vbat = jsonConfig.containsKey("enable_vbat")
                                      ? jsonConfig["enable_vbat"].as<bool>()
                                      : false;

            out.type = CommandType::SET_CONFIG;
            if (errorBuf && errorBufLen) errorBuf[0] = '\0';
            return true;
        }

        // b) SET_IDENTITY (factory)
        if (cmdObj.containsKey("identity")) {
            JsonObject ident = cmdObj["identity"];

            const char* manufacturer = ident["manufacturer"] | "UNKNOWN";
            const char* logger_type  = ident["logger_type"] | "Moonraker";
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
 * Example:
 * @code
 * {
 *   "info": {
 *     "version": "Moonraker v1.0",
 *     "compilation_date": "2025-08-04T10:22:15"
 *   }
 * }
 * @endcode
 *
 * @param version Firmware version string (e.g. "Moonraker v1.0").
 *
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* JsonProtocol::buildInfoJSON(const char* version) {
    static char buffer[128];
    StaticJsonDocument<128> doc;

    char dateCompil[23];
    // Utility function: converts build date/time into ISO8601
    convertDateToISO8601(dateCompil, sizeof(dateCompil));

    JsonObject obj          = doc.createNestedObject("info");
    obj["version"]          = version ? version : "Moonraker";
    obj["compilation_date"] = dateCompil;

    serializeJson(doc, buffer);
    return buffer;
}

/**
 * @brief Build JSON with logger identification details.
 *
 * The identity fields are provided through a @ref SetIdentityPayload structure,
 * typically filled either from factory-programmed data (flash) or from a parsed
 * SET_IDENTITY command.
 *
 * Example:
 * @code
 * {
 *   "id": {
 *     "uid_mcu":      "ABCDEF1234567890",
 *     "manufacturer": "CNRS",
 *     "date_fab":     "2025-07-23",
 *     "logger_type":  "Moonraker",
 *     "logger_sn":    "MRK-0001"
 *   }
 * }
 * @endcode
 *
 * @param payload Reference to a @ref SetIdentityPayload structure containing
 *                the logger identity fields:
 *                - UID          : MCU unique identifier string.
 *                - manufacturer : Manufacturer name.
 *                - logger_type  : Logger model/type.
 *                - date_fab     : Fabrication date (ISO8601 date string).
 *                - logger_sn    : Human-readable serial number.
 *
 * @return Pointer to a static internal buffer containing the serialized JSON.
 *         The buffer is overwritten at each call.
 */
const char* JsonProtocol::buildIdJSON(const SetIdentityPayload& payload) {
    static char buffer[192];
    StaticJsonDocument<192> doc;

    JsonObject obj      = doc.createNestedObject("id");
    obj["uid_mcu"]      = payload.UID[0] ? payload.UID : "UNKNOWN";
    obj["manufacturer"] = payload.manufacturer[0] ? payload.manufacturer : "UNKNOWN";
    obj["date_fab"]     = payload.date_fab[0] ? payload.date_fab : "";
    obj["logger_type"]  = payload.logger_type[0] ? payload.logger_type : "Moonraker";
    obj["logger_sn"]    = payload.logger_sn[0] ? payload.logger_sn : "";

    serializeJson(doc, buffer);
    return buffer;
}

/**
 * @brief Build JSON with battery voltage in millivolts.
 *
 * Example:
 * @code
 * {
 *   "vbat": {
 *     "vbat_mV": 3770
 *   }
 * }
 * @endcode
 *
 * @param voltage_mV Battery voltage in millivolts.
 *
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
 * @brief Build JSON snapshot of the current configuration.
 *
 * Uses:
 *  - @c Cfg_rb.dateCurrent as the current date/time (BCD format converted to ISO8601).
 *  - @c config as the runtime configuration for interval and sensor enable flags.
 *
 * Example:
 * @code
 * {
 *   "config": {
 *     "date_current":           "2025-08-04T10:25:00",
 *     "acquisition_interval_s": 120,
 *     "enable_light1":          true,
 *     "enable_light2":          false,
 *     "enable_vbat":            true
 *   }
 * }
 * @endcode
 *
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* JsonProtocol::buildConfigJSON() {
    static char buffer[256];
    StaticJsonDocument<256> doc;

    // Cfg_rb contains the current date/time (in BCD format)
    char dateCurrentStr[23];
    convertBcdDateToISO8601(&Cfg_rb.dateCurrent, dateCurrentStr, sizeof(dateCurrentStr));

    JsonObject obj                = doc.createNestedObject("config");
    obj["date_current"]           = dateCurrentStr;
    obj["acquisition_interval_s"] = config.acquisition_interval_s;
    obj["enable_light1"]          = config.enable_light1;
    obj["enable_light2"]          = config.enable_light2;
    obj["enable_vbat"]            = config.enable_vbat;

    serializeJson(doc, buffer);
    return buffer;
}
