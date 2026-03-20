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
 * - thresholds exposed by @ref battery_thresholds_t
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
#include "battery.h"  // battery_* types + battery_read_mv()

// /**
//  * @struct BatteryMeasureConfig
//  * @brief Board-level measurement configuration for VBAT (service API).
//  *
//  * Provided by the integration/HAL layer. Defines:
//  * - which ADC pin is used for VBAT
//  * - which divider ratio is applied (if any)
//  * - ADC reference and resolution for conversion
//  * - plausibility bounds (sanity check after scaling)
//  *
//  * This service-level configuration is intentionally simple and stable.
//  * Internally, it is converted to the driver configuration structures
//  * (@ref battery_measure_config_t and @ref BatteryAdcConfig).
//  *
//  * @ingroup BatteryService
//  */
// struct BatteryMeasureConfig
// {
//     battery_measure_config_t battery_config_t;
// };

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
bool battery_service_init(const battery_measure_config_t& cfg = {});

/**
 * @brief Override thresholds.
 */
void battery_service_set_thresholds(const battery_thresholds_t& t);

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

/**
 * @brief Get battery state.
 */
battery_state_t battery_service_get_state();

/** @} */  // end of BatteryService group
