/**
 * @file deploy_mode.h
 * @brief High-level DEPLOY mode handler for Moonraker.
 *
 * This module encapsulates the runtime behavior of the DEPLOY state:
 * - Periodic wake-up on RTC alarm
 * - Sensor acquisition via the high-level sensors API
 * - Data logging to SD card
 * - Battery health monitoring and END-OF-LIFE escalation
 *
 * @ingroup AppLayer
 */

#pragma once

#include "system_state.h"
#include "stdint.h"
#include "ir_pwm.h"

/**
 * @brief Execute one iteration of the DEPLOY state.
 *
 * This function is intended to be called from the main state machine when
 * @ref STATE_DEPLOY is active. It:
 * - Puts the MCU into sleep until the RTC alarm triggers.
 * - On wake-up, re-arms the next alarm, acquires sensor data, logs it,
 *   and performs periodic battery checks.
 *
 * On critical battery conditions, this function may change the @p state
 * to @ref STATE_ENDOFLIFE.
 *
 * @param state Reference to the current system state variable.
 */
void run_deploy_state(SystemState& state, ir_pwm& ir_driver);
