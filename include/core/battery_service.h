/**
 * @file battery_service.h
 * @brief Battery orchestration layer (tick/logs/actions).
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
#include "battery.h"

bool battery_is_available();

void battery_set_available(bool available);

/**
 * @brief Configuration of the battery signal filtering stage.
 *
 * This filter is applied at the service layer, after ADC conversion
 * and plausibility validation.
 *
 * The filtering pipeline is:
 *   raw ADC -> plausibility -> median(3) -> EMA -> output
 *
 * - Median filter removes spikes (robust to outliers).
 * - EMA (Exponential Moving Average) smooths remaining noise.
 * - Delta threshold avoids micro-variations triggering updates.
 */
struct battery_filter_config_t {
    float ema_alpha =
        0.25f;  ///< EMA smoothing factor (0..1). Lower = smoother, higher = more reactive.
    uint16_t delta_threshold_mv = 30;  ///< Minimum change (mV) to consider output as updated.
};

/**
 * @brief Runtime state of the battery filter.
 *
 * This structure holds the internal state required by the filtering
 * algorithm. It must be persistent across calls.
 *
 * It is fully managed by the battery service and should not be accessed
 * directly by user code.
 */
struct battery_filter_state_t {
    int32_t raw_samples[3] = {0, 0, 0};  ///< Last 3 raw plausible samples (mV).
    int32_t median_mv      = 0;          ///< Last median output (mV).
    int32_t ema_mv         = 0;          ///< Current EMA filtered value (mV).
    int32_t published_mv   = 0;          ///< Last significant value after deadband.
    bool initialized       = false;      ///< Initialization flag for first sample handling.
};

/**
 * @brief Battery service plausibility policy.
 */
struct battery_policy_config_t {
    uint16_t plausible_min_mv = 0;  ///< 0 = disable lower bound.
    uint16_t plausible_max_mv = 0;  ///< 0 = disable upper bound.
};

/**
 * @brief Full battery service configuration.
 *
 * Combines:
 * - hardware configuration (ADC + pin)
 * - plausibility policy
 * - filtering configuration
 */
struct battery_service_config_t {
    battery_hw_config_t hw;          ///< Hardware/ADC configuration.
    battery_policy_config_t policy;  ///< Plausibility bounds configuration.
    battery_filter_config_t filter;  ///< Filtering configuration.
};

/**
 * @brief Global battery service configuration instance.
 *
 * Defined in @c battery_service.cpp.
 */
extern battery_service_config_t batt_serv_cfg;

/**
 * @brief Initialize the battery service with board-level measurement parameters.
 *
 * Call once at boot after HAL/hardware is known.
 *
 * @param cfg Measurement configuration (pin/ratio/ADC params/plausibility).
 *
 * @ingroup BatteryService
 * @see battery_service_config_t
 * @see battery_service_apply_type_string()
 * @see battery_service_read_vbat_filtered_mv()
 * @see battery_service_read_vbat_telemetry_mv()
 */
bool battery_service_init(const battery_service_config_t& cfg);

/**
 * @brief Apply battery defaults based on a battery type string.
 *
 * Delegates parsing and default thresholds selection to the driver layer.
 * Unknown/empty strings fall back to a safe default (e.g. LiPo 1S).
 *
 * @param battery_type Battery type string.
 *
 * @ingroup BatteryService
 * @see battery_type_from_string()
 * @see battery_thresholds_default()
 */
battery_thresholds_t battery_service_apply_type_string(const String& battery_type);

/**
 * @brief Read filtered VBAT and detect significant change.
 *
 * Reports whether the filtered value changed beyond the configured deadband.
 *
 * @param[out] vbat_mv Filtered battery voltage (mV).
 * @param[out] changed True if value changed significantly since last update.
 *
 * @return true if measurement is valid.
 * @return false if error occurred. In this case @p vbat_mv contains an error code:
 *         - -1 = ADC/config error
 *         - -2 = plausibility error
 */
bool battery_service_read_vbat_filtered_mv(int32_t& vbat_mv, bool& changed);

/**
 * @brief Read VBAT for telemetry/diagnostic reporting.
 *
 * Reads the battery voltage and applies the service filter, but does not apply
 * the service plausibility bounds. This is intended for GUI requests such as
 * GET_VBAT, where the raw measured voltage should still be reported even if it
 * is outside the nominal battery operating range.
 *
 * @param[out] vbat_mv Filtered battery voltage in mV, or a negative driver error code.
 * @param[out] changed True if the filtered value changed significantly.
 *
 * @return true if the ADC read succeeded.
 * @return false if the driver returned an error.
 */
bool battery_service_read_vbat_telemetry_mv(int32_t& vbat_mv, bool& changed);

/**
 * @brief Periodic battery check with downsampling.
 *
 * Performs the runtime evaluation only every @p period calls.
 *
 * @param vbat_mv Latest battery voltage (mV). May be negative if invalid.
 * @param counter Reference to a persistent counter maintained by the caller.
 * @param period Number of calls between two evaluations (must be > 0).
 * @return true if acceptable, invalid, or ignored.
 * @return false if critical condition detected.
 *
 * @ingroup BatteryService
 */
bool battery_service_periodic_check(int32_t vbat_mv, uint8_t& counter, uint8_t period);

/**
 * @brief Convert a battery classification into service behavior.
 *
 * Logs the battery state and returns whether the system may continue.
 *
 * @param context Short context string used in logs.
 * @param vbat_mv Battery voltage in millivolts.
 * @return true if acceptable, invalid, or ignored.
 * @return false if critical condition detected.
 */
bool battery_service_decision(const char* context, int32_t vbat_mv);

/** @} */  // end of BatteryService group
