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
 *   { "command": "GET_TIME"   }
 *   { "command": "GET_CONFIG" }
 *   @endcode
 *
 * - Configuration command (SET_CONFIG):
 *   @code
 *   {
 *     "command": {
 *       "config": {
 *         "date_current": "2025-12-08T14:30:00",
 *         "use_buffer": false,
 *         "acquisition_interval_s": 60,
 *         "enable_ir": true,
 *         "enable_rfid": true,
 *         "rfid_mode": 2,
 *         "enable_vbat": true,
 *         "schedule_start_hour": 8,
 *         "schedule_start_minute": 0,
 *         "schedule_end_hour": 18,
 *         "schedule_end_minute": 30
 *       }
 *     }
 *   }
 *   @endcode
 *
 * - Time synchronization command (SET_TIME):
 *   @code
 *   {
 *     "command": {
 *       "time": {
 *         "date_current": "2026-05-27T15:23:41"
 *       }
 *     }
 *   }
 *   @endcode
 *
 * @note The GUI exposes a single @c enable_ir field. Internally this value is
 * mapped to both IR channels: @c enable_ir1 and @c enable_ir2.
 *
 * - Factory identity command (SET_IDENTITY, factory tool only):
 *   @code
 *   {
 *     "command": {
 *       "identity": {
 *         "manufacturer": "CNRS",
 *         "logger_type":  "RFID-M0",
 *         "date_fab":     "2025-01-01",
 *         "logger_sn":    "MRK-0001"
 *       }
 *     }
 *   }
 *   @endcode
 */
#include "JsonProtocol.h"

#include <ArduinoJson.h>
#include <string.h>

#include "log.h"
#include "utils.h"

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

/**
 * @brief Parse an incoming JSON command into a ParsedCommand structure.
 *
 * @param json_string Null-terminated JSON input string.
 * @param out Output structure with parsed command and payload.
 * @param errorBuf Output buffer for a small JSON error message.
 * @param errorBufLen Size of @p errorBuf in bytes.
 *
 * @return @c true if parsing succeeded and @p out.type != CommandType::NONE,
 *         @c false otherwise.
 */
bool JsonProtocol::parseCommand(const char* json_string,
                                ParsedCommand& out,
                                char* errorBuf,
                                size_t errorBufLen) {
    out.type = CommandType::NONE;

    if (!json_string) {
        writeErrorJson(errorBuf, errorBufLen, "Null JSON");
        return false;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, json_string);
    if (error) {
        writeErrorJson(errorBuf, errorBufLen, "Invalid JSON");
        return false;
    }

    JsonVariant command = doc["command"];
    if (command.isNull()) {
        writeErrorJson(errorBuf, errorBufLen, "'command' is missing");
        return false;
    }

    if (command.is<const char*>()) {
        const char* cmd = command.as<const char*>();

        if (strcmp(cmd, "GET_INFO") == 0)
            out.type = CommandType::GET_INFO;
        else if (strcmp(cmd, "GET_ID") == 0)
            out.type = CommandType::GET_ID;
        else if (strcmp(cmd, "GET_VBAT") == 0)
            out.type = CommandType::GET_VBAT;
        else if (strcmp(cmd, "SET_TIME") == 0)
            out.type = CommandType::SET_TIME;
        else if (strcmp(cmd, "GET_TIME") == 0)
            out.type = CommandType::GET_TIME;
        else if (strcmp(cmd, "GET_CONFIG") == 0)
            out.type = CommandType::GET_CONFIG;
        else if (strcmp(cmd, "SET_RUN_START") == 0) {
            out.type = CommandType::SET_RUN_START;
        } else {
            writeErrorJson(errorBuf, errorBufLen, "Unknown command");
            return false;
        }

        if (errorBuf && errorBufLen) errorBuf[0] = '\0';
        return true;
    }

    if (command.is<JsonObject>()) {
        JsonObject cmdObj = command.as<JsonObject>();

        if (cmdObj.containsKey("time")) {
            JsonObject jsonTime = cmdObj["time"];

            const char* date_current = jsonTime["date_current"];
            if (!date_current) {
                writeErrorJson(errorBuf, errorBufLen, "Missing 'date_current'");
                return false;
            }

            memset(&out.cfg, 0, sizeof(out.cfg));
            strncpy(out.cfg.dateCurrentIso, date_current, sizeof(out.cfg.dateCurrentIso) - 1);
            out.cfg.dateCurrentIso[sizeof(out.cfg.dateCurrentIso) - 1] = '\0';

            out.type = CommandType::SET_TIME;

            if (errorBuf && errorBufLen) errorBuf[0] = '\0';
            return true;
        }

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

            out.cfg.use_buffer =
                jsonConfig.containsKey("use_buffer") ? jsonConfig["use_buffer"].as<bool>() : false;

            out.cfg.acquisition_interval_s =
                jsonConfig.containsKey("acquisition_interval_s")
                    ? jsonConfig["acquisition_interval_s"].as<uint16_t>()
                    : 120;

            const bool enable_ir =
                jsonConfig.containsKey("enable_ir") ? jsonConfig["enable_ir"].as<bool>() : true;

            out.cfg.enable_ir1 = enable_ir;
            out.cfg.enable_ir2 = enable_ir;

            out.cfg.enable_rfid =
                jsonConfig.containsKey("enable_rfid") ? jsonConfig["enable_rfid"].as<bool>() : true;

            out.cfg.rfid_mode = jsonConfig.containsKey("rfid_mode")
                                    ? jsonConfig["rfid_mode"].as<uint8_t>()
                                    : (out.cfg.enable_rfid ? 1 : 0);

            out.cfg.enable_vbat =
                jsonConfig.containsKey("enable_vbat") ? jsonConfig["enable_vbat"].as<bool>() : true;

            out.cfg.schedule_start_hour = jsonConfig.containsKey("schedule_start_hour")
                                              ? jsonConfig["schedule_start_hour"].as<uint8_t>()
                                              : 8;

            out.cfg.schedule_start_minute = jsonConfig.containsKey("schedule_start_minute")
                                                ? jsonConfig["schedule_start_minute"].as<uint8_t>()
                                                : 0;

            out.cfg.schedule_end_hour = jsonConfig.containsKey("schedule_end_hour")
                                            ? jsonConfig["schedule_end_hour"].as<uint8_t>()
                                            : 18;

            out.cfg.schedule_end_minute = jsonConfig.containsKey("schedule_end_minute")
                                              ? jsonConfig["schedule_end_minute"].as<uint8_t>()
                                              : 30;

            out.type = CommandType::SET_CONFIG;
            if (errorBuf && errorBufLen) errorBuf[0] = '\0';
            return true;
        }

        if (cmdObj.containsKey("identity")) {
            JsonObject ident = cmdObj["identity"];

            const char* uid          = ident["uid_mcu"] | "UNKNOWN";
            const char* manufacturer = ident["manufacturer"] | "UNKNOWN";
            const char* logger_type  = ident["logger_type"] | "UNKNOWN";
            const char* date_fab     = ident["date_fab"] | "2025-01-01";
            const char* logger_sn    = ident["logger_sn"] | "UNKNOWN";

            memset(&out.identity, 0, sizeof(out.identity));
            strncpy(out.identity.UID, uid, sizeof(out.identity.UID) - 1);

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

/**
 * @brief Build JSON with firmware version and compilation date.
 *
 * Uses @ref convertDateToISO8601() to generate the compilation timestamp.
 *
 * @param version Firmware version string.
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* JsonProtocol::buildInfoJSON(const char* version) {
    static char buffer[192];
    StaticJsonDocument<192> doc;

    char dateCompil[24];
    convertDateToISO8601(dateCompil, sizeof(dateCompil));

    JsonObject obj          = doc.createNestedObject("info");
    obj["firmware_name"]    = "rfid_m0";
    obj["firmware_version"] = version ? version : "UNKNOWN";
    obj["compilation_date"] = dateCompil;
    obj["board"]            = __PIO_BOARD_NAME__;

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
 * @brief Build a JSON snapshot of the current logger configuration.
 *
 * Builds the response used by @c GET_CONFIG. The output format mirrors the GUI
 * configuration payload so the same field names are used in both directions.
 *
 * The two internal IR channels are exported as a single @c enable_ir field:
 * @code
 * enable_ir = enable_ir1 || enable_ir2
 * @endcode
 *
 * The JSON includes the current logger time, acquisition settings, enabled
 * modules, RFID mode, VBAT logging state, and daily activation schedule window.
 *
 * @param payload Current configuration snapshot to serialize.
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* JsonProtocol::buildConfigJSON(const ConfigResponsePayload& payload) {
    static char buffer[384];
    StaticJsonDocument<384> doc;

    JsonObject obj                = doc.createNestedObject("config");
    obj["date_current"]           = payload.dateCurrentIso;
    obj["use_buffer"]             = payload.use_buffer;
    obj["acquisition_interval_s"] = payload.acquisition_interval_s;
    obj["enable_ir"]              = payload.enable_ir1 || payload.enable_ir2;
    obj["enable_rfid"]            = payload.enable_rfid;
    obj["rfid_mode"]              = payload.rfid_mode;
    obj["enable_vbat"]            = payload.enable_vbat;
    obj["schedule_start_hour"]    = payload.schedule_start_hour;
    obj["schedule_start_minute"]  = payload.schedule_start_minute;
    obj["schedule_end_hour"]      = payload.schedule_end_hour;
    obj["schedule_end_minute"]    = payload.schedule_end_minute;

    serializeJson(doc, buffer);
    return buffer;
}

/**
 * @brief Build a GET_TIME JSON response.
 *
 * Serializes a RTC datetime using ISO-8601 format:
 * @code
 * {"time":"2026-05-27T15:30:11"}
 * @endcode
 *
 * @param dt RTC datetime to serialize.
 *
 * @return Pointer to a static JSON buffer.
 */
const char* JsonProtocol::buildTimeJSON(const DateTime& dt) {
    static char buffer[96];

    StaticJsonDocument<96> doc;

    char current[32];

    snprintf(current,
             sizeof(current),
             "%04d-%02d-%02dT%02d:%02d:%02d",
             dt.year(),
             dt.month(),
             dt.day(),
             dt.hour(),
             dt.minute(),
             dt.second());

    doc["time"] = current;

    serializeJson(doc, buffer);

    return buffer;
}

/**
 * @brief Build a SET_TIME ACK JSON response.
 *
 * Example:
 * @code
 * {"time":"ACK","applied":"2026-05-27T15:30:11"}
 * @endcode
 *
 * @param dt RTC datetime read back after update.
 *
 * @return Pointer to a static JSON buffer.
 */
const char* JsonProtocol::buildTimeAckJSON(const DateTime& dt) {
    static char buffer[128];

    StaticJsonDocument<128> doc;

    char applied[32];

    snprintf(applied,
             sizeof(applied),
             "%04d-%02d-%02dT%02d:%02d:%02d",
             dt.year(),
             dt.month(),
             dt.day(),
             dt.hour(),
             dt.minute(),
             dt.second());

    doc["time"]    = "ACK";
    doc["applied"] = applied;

    serializeJson(doc, buffer);

    return buffer;
}
