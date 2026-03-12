/**
 * @file battery_service.h
 * @defgroup BatteryService Battery Service
 * @ingroup SystemModules
 * @brief Battery orchestration layer for Moonraker (tick/logs/actions).
 *
 * This module sits above the reusable battery driver (`drivers/battery.*`) and
 * is responsible for:
 * - call cadence (periodic checks / boot checks)
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
 * - thresholds exposed by @ref BatteryThresholds
 *
 * @note The service stores a board-level measurement configuration and converts it
 *       to the driver configuration types (see @ref battery_measure_config_t and
 *       @ref BatteryAdcConfig) when calling the driver.
 *
 * @see drivers/battery.h
 * @see core/battery_service.cpp
 * @see core/error_handler.h
 * @see core/log.h
 * @{
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "drivers/battery.h"  // battery_* types + battery_read_mv()

/**
 * @struct BatteryMeasureConfig
 * @brief Board-level measurement configuration for VBAT (service API).
 *
 * Provided by the integration/HAL layer. Defines:
 * - which ADC pin is used for VBAT
 * - which divider ratio is applied (if any)
 * - ADC reference and resolution for conversion
 * - plausibility bounds (sanity check after scaling)
 *
 * This service-level configuration is intentionally simple and stable.
 * Internally, it is converted to the driver configuration structures
 * (@ref battery_measure_config_t and @ref BatteryAdcConfig).
 *
 * @ingroup BatteryService
 */
struct BatteryMeasureConfig
{
    uint32_t pin        = 0;     ///< ADC pin to read (Arduino pin id).
    float ratio         = 1.0f;  ///< Divider compensation ratio (e.g. 2.0f for /2). Must be > 0.
    uint16_t adc_ref_mv = 3300;  ///< ADC reference in mV (default: 3300).
    uint16_t adc_max    = 1023;  ///< Maximum ADC code (default: 1023 for 10-bit).

    /**
     * @brief Plausibility bounds after scaling (mV).
     *
     * Used as a sanity check to detect miswiring, floating ADC input,
     * wrong divider, etc.
     *
     * Plausibility checking is enabled only if BOTH bounds are non-zero.
     * To disable plausibility checking, set both bounds to 0.
     */
    uint16_t plausible_min_mv = 2500;  ///< Min plausible VBAT (mV). Set to 0 to disable.
    uint16_t plausible_max_mv = 4500;  ///< Max plausible VBAT (mV). Set to 0 to disable.
};

/**
 * @struct BatteryThresholds
 * @brief Voltage thresholds exposed by the battery service (mV).
 *
 * The driver owns the default thresholds per battery technology.
 * This struct allows:
 * - reading back the active thresholds (telemetry / GUI)
 * - overriding thresholds (tests or explicit configuration)
 *
 * - `warn_mv`: below this value => WARNING
 * - `critical_mv`: below this value => CRITICAL (low battery)
 * - `high_crit_mv`: above this value => CRITICAL (unexpected high voltage)
 *
 * @ingroup BatteryService
 */
struct BatteryThresholds
{
    uint16_t warn_mv      = 0;  ///< Warning threshold (mV).
    uint16_t critical_mv  = 0;  ///< Critical low threshold (mV).
    uint16_t high_crit_mv = 0;  ///< Critical high threshold (mV).
};

/**
 * @brief Initialize the battery service with board-level measurement parameters.
 *
 * Call once at boot after HAL/hardware is known.
 *
 * @param cfg Measurement configuration (pin/ratio/ADC params/plausibility).
 *
 * @ingroup BatteryService
 * @see BatteryMeasureConfig
 * @see battery_service_apply_type_string()
 * @see battery_service_read_vbat_mv()
 */
void battery_service_init(const BatteryMeasureConfig& cfg);

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
void battery_service_apply_type_string(const String& battery_type);

/**
 * @brief Override the active thresholds (service-level override).
 *
 * Useful for tests or for a configuration path that provides explicit thresholds.
 *
 * @param t Threshold values in mV.
 *
 * @ingroup BatteryService
 */
void battery_service_set_thresholds(const BatteryThresholds& t);

/**
 * @brief Return the currently active thresholds.
 *
 * @return Active @ref BatteryThresholds.
 *
 * @ingroup BatteryService
 */
BatteryThresholds battery_service_get_thresholds();

/**
 * @brief One-shot VBAT measurement (mV) using configured measurement parameters.
 *
 * Wraps the driver ADC->mV conversion with board config provided via
 * @ref battery_service_init().
 *
 * @return Battery voltage in mV, or a negative error code from the driver.
 *
 * @ingroup BatteryService
 * @see battery_read_mv()
 * @see battery_measure_config_t
 * @see BatteryAdcConfig
 */
int32_t battery_service_read_vbat_mv();

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

/** @} */  // end of BatteryService group
