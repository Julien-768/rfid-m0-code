/**
 * @file system_state.h
 * @brief Global state machine definitions.
 *
 * Defines all operational states used by the logger main application loop.
 *
 * ## State overview
 *
 * | State | Description |
 * |:------|:------------|
 * | STATE_INIT | Initial runtime state after boot sequence; selects CONNECTED or DEPLOY. |
 * | STATE_CONNECTED | Interactive configuration mode via serial/GUI. |
 * | STATE_DEPLOY | Autonomous low-power data logging mode. |
 * | STATE_STOCK | Storage / idle mode before deployment. |
 * | STATE_ENDOFLIFE | Permanent shutdown after critical error or battery failure. |
 * | STATE_ERROR | Fallback / placeholder idle state. |
 *
 * ## Boot model
 * The hardware boot and diagnostic sequence is executed once in `setup()`
 * (see @ref runBootSequence()). No dedicated BOOT state exists in the
 * runtime state machine.
 *
 * @see main.cpp
 * @ingroup SystemModules
 */

#pragma once

#include <stdint.h>

/**
 * @brief Global runtime states of the logger.
 */
enum SystemState : uint8_t {
    STATE_INIT      = 0x00,  ///< Runtime initialization and mode selection
    STATE_CONNECTED = 0x01,  ///< Host / GUI communication mode
    STATE_DEPLOY    = 0x02,  ///< Autonomous data acquisition mode
    STATE_STOCK     = 0x03,  ///< Low-power storage / idle mode
    STATE_ENDOFLIFE = 0x04,  ///< Permanent shutdown / deep sleep
    STATE_ERROR     = 0x05   ///< Fallback / placeholder idle state.
};
