/**
 * @file JsonProtocol.h
 * @brief JSON protocol helper for the logger over UART.
 *
 * This module is responsible only for:
 * - parsing incoming JSON into command payloads
 * - building outgoing JSON responses
 *
 * It does not modify global configuration, update the RTC, or initialize hardware.
 */

#pragma once

#include <Arduino.h>
#include <RTClib.h>

/**
 * @brief Supported communication commands for the JSON protocol.
 */
enum class CommandType : uint8_t {
    NONE,
    GET_INFO,
    GET_ID,
    GET_VBAT,
    SET_TIME,
    GET_TIME,
    GET_CONFIG,
    SET_CONFIG,
    SET_RUN_START,
    SET_STORAGE,
    SET_IDENTITY
};

/**
 * @brief Payload for a SET_CONFIG command.
 */
struct SetConfigPayload {
    char dateCurrentIso[32];
    bool use_buffer;
    uint16_t acquisition_interval_s;
    bool enable_light1;
    bool enable_light2;
    bool enable_rfid;
    uint8_t rfid_mode;
    bool enable_vbat;

    uint8_t schedule_start_hour;
    uint8_t schedule_start_minute;
    uint8_t schedule_end_hour;
    uint8_t schedule_end_minute;
};

/**
 * @brief Payload used to build a GET_CONFIG response.
 */
struct ConfigResponsePayload {
    char dateCurrentIso[32];
    bool use_buffer;
    uint16_t acquisition_interval_s;
    bool enable_light1;
    bool enable_light2;
    bool enable_rfid;
    uint8_t rfid_mode;
    bool enable_vbat;

    uint8_t schedule_start_hour;
    uint8_t schedule_start_minute;
    uint8_t schedule_end_hour;
    uint8_t schedule_end_minute;
};

/**
 * @brief Payload for a SET_IDENTITY command.
 */
struct SetIdentityPayload {
    char UID[32];
    char manufacturer[16];
    char logger_type[16];
    char date_fab[16];
    char logger_sn[16];
};

/**
 * @brief Parsed command structure returned by JsonProtocol::parseCommand().
 */
struct ParsedCommand {
    CommandType type{CommandType::NONE};
    SetConfigPayload cfg{};
    SetIdentityPayload identity{};
};

namespace JsonProtocol {

/**
 * @brief Parse an incoming JSON command line.
 *
 * Decodes a UART JSON message into a structured @ref ParsedCommand.
 *
 * @param[in]  json_string Input JSON string.
 * @param[out] out         Parsed command output structure.
 * @param[out] errorBuf    Output error message buffer.
 * @param[in]  errorBufLen Size of @p errorBuf.
 *
 * @retval true  JSON command successfully parsed.
 * @retval false Invalid or unsupported JSON command.
 */
bool parseCommand(const char* json_string, ParsedCommand& out, char* errorBuf, size_t errorBufLen);

/**
 * @brief Check whether a UART line looks like a JSON object.
 *
 * Used before parsing to ignore empty lines, startup noise, partial
 * messages, or non-JSON log lines received on the shared serial link.
 *
 * @param line Trimmed UART line to validate.
 *
 * @retval true  Line starts with @c { and ends with @c }.
 * @retval false Line should be ignored.
 */
bool isJsonObjectLine(const String& line);

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
bool sanitizeJsonLine(String& line);

/**
 * @brief Build a GET_INFO JSON response.
 *
 * @param version Firmware version string.
 *
 * @return Pointer to a static JSON buffer.
 */
const char* buildInfoJSON(const char* version);

/**
 * @brief Build a GET_ID JSON response.
 *
 * @param payload Identity payload structure.
 *
 * @return Pointer to a static JSON buffer.
 */
const char* buildIdJSON(const SetIdentityPayload& payload);

/**
 * @brief Build a GET_VBAT JSON response.
 *
 * @param voltage_mV Battery voltage in millivolts.
 *
 * @return Pointer to a static JSON buffer.
 */
const char* buildVbatJSON(unsigned int voltage_mV);

/**
 * @brief Build a GET_CONFIG JSON response.
 *
 * @param payload Configuration payload structure.
 *
 * @return Pointer to a static JSON buffer.
 */
const char* buildConfigJSON(const ConfigResponsePayload& payload);

/**
 * @brief Build JSON with the current RTC datetime.
 *
 * @param dt RTC datetime to serialize.
 * @return Pointer to a static internal buffer (overwritten at each call).
 */
const char* buildTimeJSON(const DateTime& dt);

}  // namespace JsonProtocol
