/**
 * @file deploy_mode.h
 * @brief High-level DEPLOY mode handler.
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

extern bool batt_available;
/**
 * @brief Initialize the DEPLOY state runtime context.
 *
 * This function must be called once when entering @ref STATE_DEPLOY.
 * It:
 * - Resets internal event flags and counters
 * - Registers RTC and IR callbacks
 * - Arms the first RTC wake-up alarm
 *
 * @param ir_driver Reference to the IR PWM driver used to bind sensor callbacks.
 */
void deploy_enter(ir_pwm& ir_driver);

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
 * @param ir_driver Reference to the IR PWM driver.
 */
void run_deploy_state(SystemState& state, ir_pwm& ir_driver);

/**
 * @brief Cleanup the DEPLOY state runtime context.
 *
 * This function must be called once when leaving @ref STATE_DEPLOY.
 * It:
 * - Unregisters RTC and IR callbacks
 * - Clears RTC alarm flag
 * - Resets internal counters and runtime state
 *
 * @param ir_driver Reference to the IR PWM driver used to unbind sensor callbacks.
 */
void deploy_exit(ir_pwm& ir_driver);
