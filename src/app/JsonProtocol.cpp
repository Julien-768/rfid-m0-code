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
 * @note The GUI exposes a single @c enable_ir field. Internally this value is
 * mapped to both IR channels: @c enable_light1 and @c enable_light2.
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
 * @brief Check whether a UART line looks like a JSON object.
 *
 * Used before parsing to ignore empty lines, startup noise, partial messages,
 * or non-JSON log lines received on the shared serial link.
 *
 * @param line Trimmed UART line to validate.
 *
 * @retval true  Line starts with @c { and ends with @c }.
 * @retval false Line should be ignored.
 */
bool JsonProtocol::isJsonObjectLine(const String& line) {
    return line.length() > 0 && line.startsWith("{") && line.endsWith("}");
}

/**
 * @brief Clean and extract a JSON object line from UART input.
 *
 * Removes non-printable characters and extracts the substring delimited
 * by the first @c { and the last @c } characters.
 *
 * This is used to tolerate UART startup noise and artifacts from the shared
 * serial link.
 *
 * @param[in,out] line Raw UART line to sanitize.
 *
 * @retval true  A valid JSON object candidate was extracted.
 * @retval false No valid JSON object found.
 */
bool JsonProtocol::sanitizeJsonLine(String& line) {
    line.trim();

    String cleaned;

    for (size_t i = 0; i < line.length(); i++) {
        const char c = line[i];

        if (c >= 32 && c <= 126) {
            cleaned += c;
        }
    }

    line = cleaned;

    const int json_start = line.indexOf('{');
    const int json_end   = line.lastIndexOf('}');

    if (json_start < 0 || json_end < json_start) {
        return false;
    }

    line = line.substring(json_start, json_end + 1);

    return isJsonObjectLine(line);
}

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

    StaticJsonDocument<384> doc;
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
        else if (strcmp(cmd, "GET_CONFIG") == 0)
            out.type = CommandType::GET_CONFIG;
        else {
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

            const bool enable_ir = jsonConfig["enable_ir"].as<bool>();

            out.cfg.enable_light1 = enable_ir;
            out.cfg.enable_light2 = enable_ir;

            out.cfg.enable_rfid =
                jsonConfig.containsKey("enable_rfid") ? jsonConfig["enable_rfid"].as<bool>() : true;

            out.cfg.rfid_mode =
                jsonConfig.containsKey("rfid_mode") ? jsonConfig["rfid_mode"].as<uint8_t>() : 2;

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
    convertDateToDisplayString(dateCompil, sizeof(dateCompil));

    JsonObject obj          = doc.createNestedObject("info");
    obj["firmware_name"]    = "rfid_m0";
    obj["firmware_version"] = version ? version : "UNKNOWN";
    obj["compilation_date"] = dateCompil;
    obj["board"]            = __PIO_BOARD_NAME__;
    serializeJson(doc, buffer);
    //LOG_INFO("%s", buffer);
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
 * enable_ir = enable_light1 || enable_light2
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
    obj["enable_ir"]              = payload.enable_light1 || payload.enable_light2;
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
