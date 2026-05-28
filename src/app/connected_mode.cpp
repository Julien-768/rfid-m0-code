/**
 * @file connected_mode.cpp
 * @defgroup Connected_Mode Connected Mode
 * @ingroup SystemModules
 * @brief UART JSON command handling for CONNECTED mode.
 *
 * This module implements the **CONNECTED** state logic of the logger.
 * In this mode, the device communicates with an external GUI/tool over UART
 * @c GUI_SERIAL using a line-based JSON protocol.
 *
 * Responsibilities
 * - Read incoming JSON commands terminated by '\\n'
 * - Parse commands into high-level structures via @ref JsonProtocol
 * - Execute requested actions (info, id, config, identity)
 * - Send JSON responses/ACK/errors back on @c GUI_SERIAL
 *
 * Supported commands
 * - GET_INFO
 *   - Returns firmware version and build metadata.
 * - GET_ID
 *   - Returns logger identification data:
 *     - MCU UID (from @ref hw_assembly.cfg / @ref Assembly::uid_mainboard)
 *     - Factory identity (from MCU Flash via @ref logger_identity.h)
 * - GET_VBAT
 *   - Returns battery voltage (mV). (Currently placeholder if not implemented.)
 * - GET_CONFIG
 *   - Returns current configuration snapshot.
 * - SET_CONFIG
 *   - Applies RTC time (from GUI), updates runtime configuration,
 *     and transitions to INIT before deployment.
 * - SET_IDENTITY (factory)
 *   - Programs the factory identity into SAMD21 internal flash
 *     via @ref device_id_program.
 *
 * Factory identity storage
 * Factory identity (manufacturer / logger type / fabrication date / serial number)
 * is stored in SAMD21 internal non-volatile memory (Flash).
 * This module does not access any EEPROM.
 *
 * @see JsonProtocol
 * @see logger_identity.h
 * @see assembly.h
 * @see rtc.h
 * @{
 */

#include "connected_mode.h"
#include "JsonProtocol.h"
#include "SerialJsonFramer.h"
#include "system_state.h"
#include "config.h"
#include "log.h"
#include "sensors.h"
#include "sensors_internal.h"
#include "rtc.h"
#include "assembly.h"
#include "logger_identity.h"
#include "sd_manager.h"
#include "utils.h"
#include "battery_service.h"
#include "hardware.h"
#include "fw_version.h"

#ifndef GUI_SERIAL
#error "GUI_SERIAL must be defined in build flags"
#endif

/**
 * @brief Send the CONNECTED ready notification once.
 *
 * Clears pending UART bytes before sending the ready message in order
 * to avoid parsing startup noise as GUI commands.
 *
 * @param ready_sent Ready flag updated after transmission.
 * @param ready_time_ms Timestamp recorded when READY is sent.
 */
static void sendConnectedReadyOnce(bool& ready_sent, uint32_t& ready_time_ms) {

    if (ready_sent) {
        return;
    }

    while (GUI_SERIAL.available()) {
        GUI_SERIAL.read();
    }

    GUI_SERIAL.println("{\"status\":\"CONNECTED_READY\"}");

    ready_time_ms = millis();

    ready_sent = true;
}

/**
 * @brief Execute CONNECTED mode command processing (UART JSON).
 *
 * This function must be called repeatedly while the system is in
 * @ref STATE_CONNECTED. It provides a short GUI configuration window before
 * normal deployment startup.
 *
 * If no UART activity is detected on @c GUI_SERIAL for a fixed timeout, the
 * function transitions to @ref STATE_INIT so the logger can continue its normal
 * boot sequence and enter DEPLOY mode.
 *
 * Processing steps:
 * 1. If no UART data is available, check the CONNECTED timeout and return.
 * 2. On UART activity, refresh the activity timestamp.
 * 3. Read one JSON message line terminated by '\\n'.
 * 4. Parse it into a @ref ParsedCommand using @ref JsonProtocol::parseCommand.
 * 5. Dispatch the command and write the corresponding JSON response.
 *
 * State transitions
 * - On CONNECTED timeout: @ref STATE_CONNECTED -> @ref STATE_INIT.
 * - On SET_CONFIG
 *   - Applies RTC time (from GUI), updates runtime configuration,
 *     and transitions to INIT before deployment.
 * - On SET_RUN_START: @ref STATE_CONNECTED -> @ref STATE_INIT.
 *
 * @param[in,out] state Current system state reference. May be updated to
 *                      @ref STATE_INIT when the timeout expires or when a
 *                      start/config command is accepted.
 *
 * @see JsonProtocol
 * @see rtc_apply_external_time
 */

void runConnectedMode(SystemState& state) {
    static constexpr uint32_t CONNECTED_TIMEOUT_MS = 30000;
    static uint32_t last_activity_ms               = millis();

    static bool ready_sent        = false;
    static uint32_t ready_time_ms = 0;

    sendConnectedReadyOnce(ready_sent, ready_time_ms);

    if ((millis() - ready_time_ms) < 300) {
        return;
    }

    if (!GUI_SERIAL.available()) {
        if ((millis() - last_activity_ms) > CONNECTED_TIMEOUT_MS) {
            LOG_INFO("CONNECTED timeout -> INIT");
            state = STATE_INIT;
        }
        return;
    }

    last_activity_ms = millis();
    // Read a full JSON line from the GUI / external tool.
    String incoming = GUI_SERIAL.readStringUntil('\n');

    if (!SerialJsonFramer::sanitizeJsonLine(incoming)) {
        return;
    }

    ParsedCommand parsed{};
    char errorJson[96] = {0};

    if (!JsonProtocol::parseCommand(incoming.c_str(), parsed, errorJson, sizeof(errorJson))) {

        GUI_SERIAL.println(errorJson);
        return;
    }

    // Dispatch command
    switch (parsed.type) {
        case CommandType::GET_INFO: {
            const char* json = JsonProtocol::buildInfoJSON(FW_VERSION_STRING);
            GUI_SERIAL.println(json);
            break;
        }

        case CommandType::GET_ID: {
            // Factory identity stored in SAMD21 flash
            const auto& id = device_id_get();

            SetIdentityPayload payload{};
            strncpy(payload.UID, hw_assembly.uid_mainboard.c_str(), sizeof(payload.UID) - 1);
            strncpy(payload.manufacturer, id.manufacturer, sizeof(payload.manufacturer) - 1);
            strncpy(payload.date_fab, id.date_fab, sizeof(payload.date_fab) - 1);
            strncpy(payload.logger_type, id.logger_type, sizeof(payload.logger_type) - 1);
            strncpy(payload.logger_sn, id.serial_number, sizeof(payload.logger_sn) - 1);

            const char* json = JsonProtocol::buildIdJSON(payload);

            GUI_SERIAL.println(json);
            break;
        }

        case CommandType::GET_VBAT: {
            if (!battery_is_available()) {
                GUI_SERIAL.println("{\"error\":\"Battery measurement not available\"}");
                break;
            } else {
                int32_t vbat_mv;
                bool changed;
                if (!battery_service_read_vbat_filtered_mv(vbat_mv, changed)) {
                    GUI_SERIAL.println("{\"error\":\"Failed to read battery voltage\"}");
                    break;
                }
                const char* json = JsonProtocol::buildVbatJSON(vbat_mv);
                GUI_SERIAL.println(json);
            }
            break;
        }

        case CommandType::GET_CONFIG: {
            ConfigResponsePayload payload{};

            DateTime now = rtc().now();

            snprintf(payload.dateCurrentIso,
                     sizeof(payload.dateCurrentIso),
                     "%04d-%02d-%02dT%02d:%02d:%02d",
                     now.year(),
                     now.month(),
                     now.day(),
                     now.hour(),
                     now.minute(),
                     now.second());
            payload.use_buffer             = config.use_buffer;
            payload.acquisition_interval_s = config.acquisition_interval_s;
            payload.enable_light1          = config.enable_light1;
            payload.enable_light2          = config.enable_light2;
            payload.enable_rfid            = config.enable_rfid;
            payload.rfid_mode              = rfidModeToUint(config.rfid_mode);
            payload.enable_vbat            = config.enable_vbat;
            payload.schedule_start_hour    = config.schedule_start_hour;
            payload.schedule_start_minute  = config.schedule_start_minute;
            payload.schedule_end_hour      = config.schedule_end_hour;
            payload.schedule_end_minute    = config.schedule_end_minute;

            const char* json = JsonProtocol::buildConfigJSON(payload);

            GUI_SERIAL.println(json);
            break;
        }

        case CommandType::SET_CONFIG: {
            LOG_INFO("Configuration received from GUI");

            DateTime dt;
            if (!convertISO8601ToDateTime(parsed.cfg.dateCurrentIso, &dt)) {
                LOG_ERROR("Invalid date received from GUI");
                GUI_SERIAL.println("{\"error\":\"Invalid date_current\"}");
                break;
            }

            config.use_buffer             = parsed.cfg.use_buffer;
            config.acquisition_interval_s = parsed.cfg.acquisition_interval_s;
            config.enable_light1          = parsed.cfg.enable_light1;
            config.enable_light2          = parsed.cfg.enable_light2;
            config.enable_rfid            = parsed.cfg.enable_rfid;
            config.rfid_mode              = rfidModeFromUint(parsed.cfg.rfid_mode);
            config.enable_vbat            = parsed.cfg.enable_vbat;
            config.schedule_start_hour    = parsed.cfg.schedule_start_hour;
            config.schedule_start_minute  = parsed.cfg.schedule_start_minute;
            config.schedule_end_hour      = parsed.cfg.schedule_end_hour;
            config.schedule_end_minute    = parsed.cfg.schedule_end_minute;

            validateConfig(config);

            if (hw_assembly.rtc_type == "ds3231") {
                rtc_apply_external_time(dt);
                LOG_INFO("RTC adjusted successfully from GUI (SET_CONFIG)");
            }

            if (strlen(get_filename()) == 0) {
                check_and_create_new_daily_file(rtc().now());
                LOG_INFO("Daily file created after GUI time; buffered logs will be flushed");
            }

            GUI_SERIAL.println("{\"config\":\"ACK\"}");

            LOG_INFO("Deploy mode started from GUI");
            state = STATE_INIT;
            break;
        }

        case CommandType::SET_RUN_START: {
            GUI_SERIAL.println("{\"config\":\"ACK\"}");

            LOG_INFO("Deploy mode started from GUI");
            state = STATE_INIT;
            break;
        }
        case CommandType::SET_IDENTITY: {
            LOG_INFO("Factory SET_IDENTITY command received");

            device_id_applyFromFields(parsed.identity.manufacturer,
                                      parsed.identity.logger_type,
                                      parsed.identity.date_fab,
                                      parsed.identity.logger_sn);

            GUI_SERIAL.println("{\"identity\":\"ACK\"}");
            break;
        }

        case CommandType::SET_STORAGE: {
            GUI_SERIAL.println("{\"storage\":\"ACK\"}");
            break;
        }

        default:
            // CommandType::NONE or commands not handled here
            break;
    }
}
