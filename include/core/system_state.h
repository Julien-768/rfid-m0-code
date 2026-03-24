/**
 * @file system_state.h
 * @brief Global state machine definitions for Moonraker.
 *
 * Defines all operational states used by the Moonraker main application loop.
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
 * | STATE_WAIT | Fallback / placeholder idle state. |
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
 * @brief Global runtime states of the Moonraker logger.
 */
enum SystemState : uint8_t {
    STATE_INIT,       ///< Runtime initialization and mode selection
    STATE_CONNECTED,  ///< Host / GUI communication mode
    STATE_DEPLOY,     ///< Autonomous data acquisition mode
    STATE_STOCK,      ///< Low-power storage / idle mode
    STATE_ENDOFLIFE,  ///< Permanent shutdown / deep sleep
    STATE_WAIT        ///< Idle fallback state
};
