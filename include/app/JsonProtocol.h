/**
 * @file JsonProtocol.h
 * @brief JSON protocol helper for the device over UART.
 *
 * @section authors Authors
 * - Julien Courtecuisse
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: julien.courtecuisse@iphc.cnrs.fr
 *
 * @section project Project
 * Name: RFID-M0
 * Description: Embedded firmware for the open-source RFID-M0 project, designed to monitor small animal species around burrows and nest boxes. The software integrates RFID-based individual identification, infrared passage detection, real-time clock (RTC) timestamping, and SD-card data logging, with low-power management for autonomous field deployments. It supports ecological research, including long-term monitoring of Great Tits (Parus major) and research on the European hamster (Cricetus cricetus) conducted within the cross-border CRICETUS project.
 * @version 3.0.0
 * @date 2022-08-17
 *
 * @section contributors Contributors
 * - Robin Laesser
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: robin.laesser@iphc.cnrs.fr
 *
 * @section license License
 * SPDX-FileCopyrightText: 2022 Julien Courtecuisse
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2022 Julien Courtecuisse
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * License: GPL-3.0-or-later
 * https://www.gnu.org/licenses/gpl-3.0.html
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
    bool enable_ir1;
    bool enable_ir2;
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
    bool enable_ir1;
    bool enable_ir2;
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
    char device_type[16];
    char date_fab[16];
    char device_sn[16];
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
 * @brief Build a GET_INFO JSON response.
 *
 * The response contains firmware name, firmware version, firmware compilation
 * timestamp and PlatformIO board name.
 *
 * @param version Firmware version string, or nullptr if unknown.
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
 * @brief Build a SET_TIME ACK JSON response.
 *
 * Serializes the RTC datetime read back after applying a SET_TIME command.
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
const char* buildTimeAckJSON(const DateTime& dt);

/**
 * @brief Build a GET_TIME JSON response.
 *
 * Serializes the current RTC datetime in ISO-8601 format.
 *
 * Example:
 * @code
 * {"time":"2026-05-27T15:30:11"}
 * @endcode
 *
 * @param dt RTC datetime to serialize.
 *
 * @return Pointer to a static JSON buffer.
 */
const char* buildTimeJSON(const DateTime& dt);

}  // namespace JsonProtocol
