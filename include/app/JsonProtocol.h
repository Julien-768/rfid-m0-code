/**
 * @file JsonProtocol.h
 * @brief JSON protocol helper for the logger over UART.
 *
 * This module is responsible ONLY for:
 * - parsing incoming JSON into a high-level command structure
 * - building JSON responses (info, id, vbat, config)
 *
 * It does NOT:
 * - modify the RTC
 * - change the system state
 * - initialize sensors or hardware
 *
 * Those responsibilities remain in the application / HAL layers.
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Supported communication commands for the JSON protocol.
 */
enum class CommandType : uint8_t {
    NONE,           ///< No command / parsing failed.
    GET_INFO,       ///< Request firmware version and compilation date.
    GET_ID,         ///< Request logger identification data.
    GET_VBAT,       ///< Request battery voltage.
    GET_CONFIG,     ///< Request current configuration.
    SET_CONFIG,     ///< Send a new runtime configuration and current date/time.
    SET_RUN_START,  ///< Reserved for future use.
    SET_STORAGE,    ///< Reserved for future use.
    SET_IDENTITY    ///< Factory-only: program identity (parsed here, not flashed).
};

/**
 * @brief Payload for a SET_CONFIG command.
 *
 * All fields are parsed from JSON and stored as simple types.
 * The date/time is kept as an ISO8601 string; conversion to RTC-specific
 * types is handled elsewhere.
 */
struct SetConfigPayload {
    char dateCurrentIso[32];          ///< ISO8601 date/time string, e.g. "2025-08-04T10:30:00".
    uint16_t acquisition_interval_s;  ///< Logging period in seconds.
    bool enable_light1;               ///< Enable or disable light sensor 1.
    bool enable_light2;               ///< Enable or disable light sensor 2.
    bool enable_rfid;                 ///< Enable or disable the RFID reader.
    uint8_t rfid_mode;                ///< RFID mode (0=OFF, 1=CONTINUOUS, 2=ON_IR_EVENT).
    bool enable_vbat;                 ///< Enable or disable battery voltage logging.
};

/**
 * @brief Payload for a SET_IDENTITY command (factory tool only).
 *
 * These are fixed-size string buffers.
 * JsonProtocol does not define how identity is stored in MCU flash.
 * That mapping is handled in higher-level code.
 */
struct SetIdentityPayload {
    char UID[32];           ///< MCU unique ID string, e.g. "ABCDEF1234567890".
    char manufacturer[16];  ///< Manufacturer name, e.g. "CNRS".
    char logger_type[16];   ///< Logger type, e.g. "Moonraker".
    char date_fab[16];      ///< Fabrication date, e.g. "2025-01-01".
    char logger_sn[16];     ///< Human-readable serial number, e.g. "MRK-0001".
};

/**
 * @brief Parsed command structure returned by JsonProtocol::parseCommand().
 *
 * - For GET_* commands, only @ref type is meaningful.
 * - For SET_CONFIG, @ref cfg contains the new runtime configuration payload.
 * - For SET_IDENTITY, @ref identity contains the factory identity payload.
 */
struct ParsedCommand {
    CommandType type{CommandType::NONE};  ///< Parsed command type.
    SetConfigPayload cfg{};               ///< Payload for SET_CONFIG.
    SetIdentityPayload identity{};        ///< Payload for SET_IDENTITY.
};

namespace JsonProtocol {
/**
 * @brief Parse an incoming JSON command string.
 *
 * Supported formats:
 *
 * - Simple commands:
 * @code
 * { "command": "GET_INFO"   }
 * { "command": "GET_ID"     }
 * { "command": "GET_VBAT"   }
 * { "command": "GET_CONFIG" }
 * @endcode
 *
 * - Configuration command (SET_CONFIG):
 * @code
 * {
 *   "command": {
 *     "config": {
 *       "date_current": "2025-08-04T10:30:00",
 *       "acquisition_interval_s": 120,
 *       "enable_light1": true,
 *       "enable_light2": false,
 *       "enable_rfid": true,
 *       "rfid_mode": 2,
 *       "enable_vbat": true
 *     }
 *   }
 * }
 * @endcode
 *
 * - Factory identity command (SET_IDENTITY, factory tool only):
 * @code
 * {
 *   "command": {
 *     "identity": {
 *       "manufacturer": "CNRS",
 *       "logger_type":  "Moonraker",
 *       "date_fab":     "2025-01-01",
 *       "logger_sn":    "MRK-0001"
 *     }
 *   }
 * }
 * @endcode
 *
 * @param json_string Null-terminated input JSON string.
 * @param out Output structure filled with the parsed command.
 * @param errorBuf Buffer where a small JSON error string may be written,
 *                 e.g. @code {"Error":"...'command' is missing"} @endcode.
 * @param errorBufLen Size of @p errorBuf in bytes.
 *
 * @return @c true if parsing succeeded and @p out.type != CommandType::NONE,
 *         @c false otherwise.
 *
 * @note On error, @p errorBuf contains a small JSON error response suitable
 *       for printing with Serial1.println(), or an empty string if no detail
 *       is available.
 */
bool parseCommand(const char* json_string, ParsedCommand& out, char* errorBuf, size_t errorBufLen);

/**
 * @brief Build JSON containing firmware version and compilation date.
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
 * @param version Null-terminated firmware version string.
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* buildInfoJSON(const char* version);

/**
 * @brief Build JSON with logger identification details.
 *
 * Example:
 * @code
 * {
 *   "id": {
 *     "uid_mcu":      "...",
 *     "manufacturer": "CNRS",
 *     "date_fab":     "2025-07-23",
 *     "logger_type":  "Moonraker",
 *     "logger_sn":    "MRK-0001"
 *   }
 * }
 * @endcode
 *
 * @param payload Identity payload structure.
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* buildIdJSON(const SetIdentityPayload& payload);

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
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* buildVbatJSON(unsigned int voltage_mV);

/**
 * @brief Build a JSON snapshot of the current configuration.
 *
 * Uses:
 * - @c gui_time_sync_rb for the current date/time
 * - @c config for runtime configuration values
 *
 * Example:
 * @code
 * {
 *   "config": {
 *     "date_current":           "2025-08-04T10:25:00",
 *     "acquisition_interval_s": 120,
 *     "enable_light1":          true,
 *     "enable_light2":          false,
 *     "enable_rfid":            true,
 *     "rfid_mode":              2,
 *     "enable_vbat":            true
 *   }
 * }
 * @endcode
 *
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* buildConfigJSON();
}  // namespace JsonProtocol
