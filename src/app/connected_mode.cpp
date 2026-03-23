/**
 * @file connected_mode.cpp
 * @defgroup Connected_Mode Connected Mode
 * @ingroup SystemModules
 * @brief UART JSON command handling for Moonraker (CONNECTED mode).
 *
 * This module implements the **CONNECTED** state logic of the Moonraker logger.
 * In this mode, the device communicates with an external GUI/tool over UART
 * (`Serial1`) using a line-based JSON protocol.
 *
 * ## Responsibilities
 * - Read incoming JSON commands terminated by '\\n'
 * - Parse commands into high-level structures via @ref JsonProtocol
 * - Execute requested actions (info, id, config, identity)
 * - Send JSON responses/ACK/errors back on `Serial1`
 *
 * ## Supported commands
 * - **GET_INFO**
 *   - Returns firmware version and build metadata.
 * - **GET_ID**
 *   - Returns logger identification data:
 *     - MCU UID (from @ref hw_assembly.cfg / @ref Assembly::uid_mainboard)
 *     - Factory identity (from MCU Flash via @ref logger_identity.h)
 * - **GET_VBAT**
 *   - Returns battery voltage (mV). (Currently placeholder if not implemented.)
 * - **GET_CONFIG**
 *   - Returns current configuration snapshot.
 * - **SET_CONFIG**
 *   - Applies RTC time (from GUI), updates runtime configuration,
 *     schedules next wake-up, initializes sensors, and transitions to DEPLOY.
 * - **SET_IDENTITY** (factory)
 *   - Programs the factory identity into **SAMD21 internal flash**
 *     via @ref loggerIdentity_program.
 *
 * ## Factory identity storage
 * Factory identity (manufacturer / logger type / fabrication date / serial number)
 * is stored in **SAMD21 internal non-volatile memory (Flash)**.
 * This module does not access any EEPROM.
 *
 * @see JsonProtocol
 * @see logger_identity.h
 * @see hw_assembly.h
 * @see rtc.h
 * @{
 */

#include "connected_mode.h"
#include "JsonProtocol.h"
#include "system_state.h"
#include "config.h"
#include "log.h"
#include "sensors.h"
#include "sensors_internal.h"
#include "rtc.h"
#include "hw_assembly.h"
#include "logger_identity.h"
#include "sd_manager.h"
#include "utils.h"
#include "battery.h"

/**
 * @brief Execute CONNECTED mode command processing (UART JSON).
 *
 * This function must be called repeatedly while the system is in
 * @ref STATE_CONNECTED. It performs a single iteration of the command loop:
 *
 * 1. If no UART data is available, returns immediately.
 * 2. Reads one JSON message line (terminated by '\\n').
 * 3. Parses it into a @ref ParsedCommand using @ref JsonProtocol::parseCommand.
 * 4. Dispatches the command and writes the corresponding JSON response.
 *
 * ### State transitions
 * - On **SET_CONFIG** success, this function sets @p state to @ref STATE_DEPLOY.
 *
 * ### Factory identity programming
 * - On **SET_IDENTITY**, the function builds a @ref LoggerIdentityFlash record
 *   from the received JSON payload and calls @ref loggerIdentity_program.
 * - On failure, a JSON error is returned.
 *
 * @param[in,out] state Current system state reference. May be updated to
 *                      @ref STATE_DEPLOY when SET_CONFIG is accepted.
 *
 * @see loggerIdentity_get
 * @see loggerIdentity_program
 * @see rtc_applyExternalTime
 * @see rtc_scheduleNextWake
 */

void runConnectedMode(SystemState& state) {
    if (!Serial1.available()) return;

    // Read a full JSON line from the GUI / external tool
    String incoming = Serial1.readStringUntil('\n');

    ParsedCommand parsed{};
    char errorJson[96] = {0};

    // Parse JSON into a high-level command structure
    if (!JsonProtocol::parseCommand(incoming.c_str(), parsed, errorJson, sizeof(errorJson)))
        {
            if (errorJson[0] != '\0')
                {
                    // Return a small JSON error message to the GUI
                    Serial1.println(errorJson);
            }
            return;
    }

    // Dispatch command
    switch (parsed.type)
        {
                case CommandType::GET_INFO: {
                    // Firmware version / compile date
                    const char* json = JsonProtocol::buildInfoJSON("Moonraker v1.0");
                    Serial1.println(json);
                    break;
                }

                case CommandType::GET_ID: {
                    // Factory identity stored in SAMD21 flash
                    const auto& id = loggerIdentity_get();

                    SetIdentityPayload payload{};
                    strncpy(payload.UID, hw_assembly.uid_mainboard.c_str(), sizeof(payload.UID));
                    strncpy(payload.manufacturer, id.manufacturer, sizeof(payload.manufacturer));
                    strncpy(payload.date_fab, id.date_fab, sizeof(payload.date_fab));
                    strncpy(payload.logger_type, id.logger_type, sizeof(payload.logger_type));
                    strncpy(payload.logger_sn, id.serial_number, sizeof(payload.logger_sn));

                    const char* json = JsonProtocol::buildIdJSON(payload);

                    Serial1.println(json);
                    break;
                }

                case CommandType::GET_VBAT: {
                    // One-shot battery measurement for GUI request (not used in DEPLOY loop)
                    uint16_t vbat_mv = read_battery_voltage(batt_cfg.pin);  // volts in mV (e.g. 3700)
                    const char* json = JsonProtocol::buildVbatJSON(vbat_mv);
                    Serial1.println(json);
                    break;
                }

                case CommandType::GET_CONFIG: {
                    const char* json = JsonProtocol::buildConfigJSON();
                    Serial1.println(json);
                    break;
                }

                case CommandType::SET_CONFIG: {
                    LOG_INFO("Configuration received from GUI");

                    DateTime dt = applyGuiConfigAndBuildDateTime(parsed.cfg);

                    rtc_apply_external_time(dt);
                    // If no daily file exists yet, create it now
                    if (strlen(get_filename()) == 0)
                        {
                            check_and_create_new_daily_file(rtc.now());
                            LOG_INFO("Daily file created after GUI time; buffered logs will be flushed");
                    }
                    LOG_INFO("RTC adjusted successfully from GUI (SET_CONFIG)");

                    rtc_schedule_next_wake(rtc.now(), config.acquisition_interval_s);
                    LOG_INFO("RTC alarm scheduled from GUI config");

                    Sensors_InitForDeploy(g_sensors, G_SENSOR_COUNT);
                    LOG_INFO("Sensors initialized from GUI configuration");

                    Serial1.println("{\"config\":\"ACK\"}");

                    LOG_INFO("Deploy mode started from GUI");
                    state = STATE_DEPLOY;
                    break;
                }

                case CommandType::SET_IDENTITY: {
                    LOG_INFO("Factory SET_IDENTITY command received");

                    loggerIdentity_applyFromFields(parsed.identity.manufacturer, parsed.identity.logger_type, parsed.identity.date_fab,
                                                   parsed.identity.logger_sn);

                    Serial1.println("{\"identity\":\"ACK\"}");
                    break;
                }

            default:
                // CommandType::NONE or commands not handled here
                break;
        }
}
