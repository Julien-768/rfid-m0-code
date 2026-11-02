/**
 * @file deploy_mode.h
 * @brief High-level DEPLOY mode handler.
 *
 * @version 3.0.0
 * @date 2022-08-17
 *
 * @section authors Authors
 * - Julien Courtecuisse
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: julien.courtecuisse@iphc.cnrs.fr
 *
 * @section project Project
 * Name: RFID-M0
 * Description: Embedded firmware for the open-source RFID-M0 project, designed to monitor small animal species around burrows and nest boxes. The software integrates RFID-based individual identification, infrared passage detection, real-time clock (RTC) timestamping, and SD-card data logging, with low-power management for autonomous field deployments. It supports ecological research, including long-term monitoring of Great Tits (Parus major) and research on the European hamster (Cricetus cricetus) conducted within the cross-border CRICETUS project.
 *
 * @section contributors Contributors
 * - Robin Laesser
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: robin.laesser@iphc.cnrs.fr
 *
 * @section license License
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

#include "system_state.h"
#include "stdint.h"
#include "ir_pwm.h"
#include "rfid_driver.h"

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
 * @param rfid_driver Reference to the RFID driver used to bind RFID callbacks.
 */
void deploy_enter(ir_pwm&);

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
 * @param rfid_driver Reference to the RFID driver.
 */
void run_deploy_state(SystemState& state, rfid_driver_t& rfid_driver, ir_pwm& ir_driver);

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
 * @param rfid_driver Reference to the RFID driver used to unbind RFID callbacks.

 */
void deploy_exit(ir_pwm& ir_driver);
