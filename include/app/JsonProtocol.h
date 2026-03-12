/**
 * @file JsonProtocol.h
 * @brief JSON protocol helper for the Moonraker logger over UART.
 *
 * This module is responsible ONLY for:
 *  - Parsing incoming JSON into a high-level command structure.
 *  - Building JSON responses (info, id, vbat, config).
 *
 * It does NOT:
 *  - Modify the RTC.
 *  - Change the system state.
 *  - Initialize sensors or hardware.
 *
 * Those responsibilities remain in connected_mode / HAL layers.
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Supported communication commands for the JSON protocol.
 */

enum class CommandType : std::uint8_t
{
    NONE,           ///< No command / parsing failed.
    GET_INFO,       ///< Request firmware version and compilation date.
    GET_ID,         ///< Request logger identification data.
    GET_VBAT,       ///< Request battery voltage.
    GET_CONFIG,     ///< Request current configuration.
    SET_CONFIG,     ///< Send a new configuration (date, period, sensor flags).
    SET_RUN_START,  ///< Reserved for future use (start acquisition).
    SET_STORAGE,    ///< Reserved for future use (change storage mode).
    SET_IDENTITY    ///< Factory-only: program identity (parsed here, not flashed).
};

/**
 * @brief Payload for a SET_CONFIG command.
 *
 * All fields are parsed from JSON and stored as simple types.
 * The date is kept as an ISO8601 string, conversion to RTC types is done elsewhere.
 */
struct SetConfigPayload
{
    char dateCurrentIso[32];          ///< ISO8601 date/time string, e.g. "2025-08-04T10:30:00".
    uint32_t acquisition_interval_s;  ///< Logging period in seconds.
    bool enable_light1;               ///< Enable or disable light sensor 1.
    bool enable_light2;               ///< Enable or disable light sensor 2.
    bool enable_vbat;                 ///< Enable or disable battery voltage logging.
};

/**
 * @brief Payload for a SET_IDENTITY command (factory tool only).
 *
 * These are just fixed-size string buffers.
 * JsonProtocol does not know how the identity is stored in MCU flash.
 * The mapping to @c LoggerIdentityFlash is done in higher-level code.
 */
struct SetIdentityPayload
{
    char UID[32];           ///< MCU unique ID string (e.g. "ABCDEF1234567890").
    char manufacturer[16];  ///< Manufacturer name, e.g. "CNRS".
    char logger_type[16];   ///< Logger type, e.g. "Moonraker".
    char date_fab[16];      ///< Fabrication date, e.g. "2025-01-01".
    char logger_sn[16];     ///< Human-readable serial number, e.g. "MRK-0001".
};

/**
 * @brief Parsed command structure returned by JsonProtocol::parseCommand().
 *
 * - For GET_* commands, only @ref type is meaningful.
 * - For SET_CONFIG, @ref cfg contains the new configuration payload.
 * - For SET_IDENTITY, @ref identity contains the factory identity payload.
 */
struct ParsedCommand
{
    CommandType type{CommandType::NONE};  ///< Parsed command type.
    SetConfigPayload cfg{};               ///< Payload for SET_CONFIG.
    SetIdentityPayload identity{};        ///< Payload for SET_IDENTITY.
};

namespace JsonProtocol
{
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
 * @param out         Output structure filled with the parsed command.
 * @param errorBuf    Buffer where a small JSON error string may be written,
 *                    e.g. @code {"Error":"...'command' is missing"} @endcode.
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
 * @param version Null-terminated firmware version string
 *                (e.g. "Moonraker v1.0").
 *
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
 * @param UID          MCU unique ID string (may be @c nullptr).
 * @param manufacturer Manufacturer string (may be @c nullptr).
 * @param date         Fabrication date string (may be @c nullptr).
 * @param logger_type  Logger type string (may be @c nullptr).
 * @param logger_sn    Human-readable serial number (may be @c nullptr).
 *
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* buildIdJSON(const char* UID, const char* manufacturer, const char* date, const char* logger_type, const char* logger_sn);

/**
 * @brief Build JSON with battery voltage (in millivolts).
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
const char* buildVbatJSON(unsigned int voltage_mV);

/**
 * @brief Build JSON snapshot of the current configuration.
 *
 * Uses Cfg_rb for the current date/time and @c config for runtime flags.
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
const char* buildConfigJSON();
}  // namespace JsonProtocol
