/**
 * @file connected_mode.h
 * @brief Interface for the UART/JSON connected mode handler.
 *
 * Connected mode allows the device to communicate with a host application
 * (e.g., LoggerApp) via UART using JSON messages. This mode is typically used
 * for:
 * - Retrieving device information and status
 * - Sending configuration data
 * - Initiating specific commands before deployment
 */

#pragma once

#include <Arduino.h>
#include "system_state.h"

/**
 * @brief Executes the connected mode command handler.
 *
 * Listens for JSON commands on the UART interface, parses them,
 * and performs the requested actions (e.g., GET_INFO, SET_CONFIG).
 * May change the system state to transition into other modes such
 * as @ref STATE_DEPLOY based on received commands.
 *
 * @param state Reference to the global system state variable.
 *              The function can update this to change the device mode.
 */
void runConnectedMode(SystemState& state);
