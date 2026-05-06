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

/**
 * @brief Supported communication commands for the JSON protocol.
 */
enum class CommandType : uint8_t {
    NONE,
    GET_INFO,
    GET_ID,
    GET_VBAT,
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
bool parseCommand(const char* json_string, ParsedCommand& out, char* errorBuf, size_t errorBufLen);

const char* buildInfoJSON(const char* version);
const char* buildIdJSON(const SetIdentityPayload& payload);
const char* buildVbatJSON(unsigned int voltage_mV);
const char* buildConfigJSON(const ConfigResponsePayload& payload);
}  // namespace JsonProtocol
