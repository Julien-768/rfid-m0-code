/**
 * @file battery_service.h
 * @defgroup BatteryService Battery Service
 * @ingroup SystemModules
 * @brief Battery orchestration layer (tick/logs/actions).
 *
 * This module sits above the reusable battery driver (`drivers/battery.*`) and
 * is responsible for:
 * - call cadence (periodic checks)
 * - logging and telemetry
 * - triggering system actions on critical battery state (error handler)
 *
 * The underlying driver remains **policy-free** and **reusable**:
 * - battery technology model (type + thresholds)
 * - ADC -> mV conversion
 * - classification (NORMAL/WARNING/CRITICAL/INVALID)
 * - no logs, no shutdown, no error handler calls
 *
 * ## Unit convention
 * All voltages handled by this module are expressed in **millivolts (mV)**:
 * - readings returned by @ref battery_service_read_vbat_mv()
 * - thresholds exposed by @ref battery_thresholds_t
 *
 * @note The service stores a board-level measurement configuration and converts it
 *       to the driver configuration types when calling the driver.
 *
 * @see drivers/battery.h
 * @see core/battery_service.cpp
 * @see core/error_handler.h
 * @see core/log.h
 * @{
 */

#pragma once

#include <Arduino.h>
#include "battery.h"

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
    int32_t published_mv   = 0;      ///< Last value considered "significant" (deadband applied).
    bool initialized       = false;  ///< Initialization flag (first sample handling).
};

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
 * @brief Initialize the battery service with board-level measurement parameters.
 *
 * Call once at boot after HAL/hardware is known.
 *
 * @param cfg Measurement configuration (pin/ratio/ADC params/plausibility).
 *
 * @ingroup BatteryService
 * @see battery_service_config_t
 * @see battery_service_apply_type_string()
 * @see battery_service_read_vbat_mv()
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
 * Same processing as @ref battery_service_read_vbat_mv(), but also reports
 * whether the filtered value changed beyond the configured deadband.
 *
 * @param[out] vbat_mv Filtered battery voltage (mV)
 * @param[out] changed True if value changed significantly since last update
 *
 * @return true if measurement is valid
 *         false if error occurred (vbat_mv contains error code)
 *
 * Error codes:
 *   -1 = ADC/config error
 *   -2 = plausibility error
 */
bool battery_service_read_vbat_filtered_mv(int32_t& vbat_mv, bool& changed);

/**
 * @brief Periodic battery check with downsampling.
 *
 * Performs the runtime evaluation only every @p period calls.
 *
 * @param vbat_mv Latest battery voltage (mV). May be negative if invalid.
 * @param counter Reference to a persistent counter maintained by the caller.
 * @param period Number of calls between two evaluations (must be > 0).
 * @return `true` if acceptable (or invalid/ignored),
 *         `false` if critical condition detected.
 *
 * @ingroup BatteryService
 */
bool battery_service_periodic_check(int32_t vbat_mv, uint8_t& counter, uint8_t period);

bool battery_service_decision(const char* context, int32_t vbat_mv);

/** @} */  // end of BatteryService group
