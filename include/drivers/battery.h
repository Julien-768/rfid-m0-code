#ifndef BATTERY_H
#define BATTERY_H

#pragma once

#include <Arduino.h>
#include <stdint.h>

/**
 * @file battery.h
 * @defgroup Battery Battery (Driver)
 * @ingroup Drivers
 * @brief Battery driver: ADC -> mV conversion + battery technology model (type/thresholds/classification).
 *
 * This driver is **reusable** and intended to be used across Arduino/Feather projects.
 *
 * It provides:
 * - ADC -> millivolts conversion (board-agnostic)
 * - Battery technology model (type + default thresholds)
 * - Classification of a voltage sample as NORMAL/WARNING/CRITICAL/INVALID
 *
 * ## Responsibilities
 * - Read an ADC pin via `analogRead()`
 * - Convert ADC code to millivolts using a provided ADC reference
 * - Apply an optional scaling factor (e.g. resistor divider compensation)
 * - Optionally validate the result against a plausibility range
 * - Provide default thresholds by battery technology
 * - Classify a measured voltage according to thresholds
 *
 * ## Non-responsibilities
 * The following concerns are handled by an upper layer (e.g. `core/battery_service.*`):
 * - Logging / telemetry
 * - Triggering system actions (shutdown / END-OF-LIFE / error handler)
 * - Call cadence ("tick" policy)
 * - Configuration storage (files, JSON, etc.)
 *
 * ## Unit convention
 * All voltages are expressed in **millivolts (mV)**.
 *
 * @see battery.cpp
 * @{
 */

/**
 * @brief Battery technology / chemistry identifier.
 *
 * This represents intrinsic battery families, used to select default thresholds.
 */
enum class battery_type_t : uint8_t
{
    battery_lipo_1s    = 0,   ///< LiPo single-cell.
    battery_liion_1s   = 1,   ///< Li-ion single-cell.
    battery_lifepo4_1s = 2,   ///< LiFePO4 single-cell.
    battery_lead_12v   = 3,   ///< Lead-acid 12V.
    battery_unknown    = 255  ///< Unknown or invalid battery type.
};

/**
 * @brief Classification of a VBAT sample according to predefined thresholds.
 *
 * The driver only classifies the battery level. It does not log events nor take automatic actions.
 * Use this enum to determine the state of the battery based on voltage or capacity measurements.
 */
enum class battery_level_t : uint8_t
{
    battery_invalid,        ///< Invalid measurement (e.g. negative voltage).
    battery_critical_high,  ///< Battery voltage is too high.
    battery_critical_low,   ///< Battery voltage is too low.
    battery_warning_low,    ///< Battery voltage is low (warning level).
    battery_normal          ///< Battery voltage is normal.
};

/**
 * @brief Battery thresholds in millivolts (mV).
 *
 * Typical policy:
 * - If vbat_mv < low_warn_mv  => WARNING
 * - If vbat_mv < low_crit_mv  => CRITICAL (too low)
 * - If vbat_mv > high_crit_mv => CRITICAL (unexpected high voltage)
 *
 * @note `high_crit_mv` is intentionally explicit (not computed) because it depends on chemistry.
 */
struct battery_thresholds_t
{
    uint16_t low_warn_mv  = 0;  ///< Warning threshold for low voltage (mV).
    uint16_t low_crit_mv  = 0;  ///< Critical threshold for low voltage (mV).
    uint16_t high_crit_mv = 0;  ///< Critical threshold for high voltage (mV).
};

typedef struct
{
    uint16_t adc_ref_mv = 3300;  ///< ADC reference in mV (default: 3300).;
    uint16_t adc_max    = 1023;  ///< Maximum ADC code (default: 1023 for 10-bit).
    float ratio         = 1.0f;  ///< Scaling factor after ADC conversion (default: 1.0f).
} battery_adc_config_t;

/**
 * @brief ADC + scaling configuration for a VBAT measurement.
 *
 * This is board/integration information, provided by the application/HAL.
 */
struct battery_measure_config_t
{
    uint32_t pin = 0;              ///< ADC pin to read (Arduino pin id).
    battery_adc_config_t adc_cfg;  ///< ADC conversion parameters (reference, max code, scaling ratio).

    /**
     * @brief Plausibility bounds after scaling (mV).
     *
     * If both bounds are non-zero, the computed voltage must fall within this range.
     * To disable plausibility checking, set both bounds to 0.
     */
    uint16_t plausible_min_mv = 2500;  ///< Min plausible VBAT (mV) or 0 to disable.
    uint16_t plausible_max_mv = 4500;  ///< Max plausible VBAT (mV) or 0 to disable.
};

/**
 * @brief Convert a configuration string to a battery type.
 *
 * Typical accepted strings:
 * - "lipo_1s"
 * - "liion_1s"
 * - "lifepo4_1s"
 * - "lead_12v"
 *
 * Unknown or null strings fall back to a safe default (LiPo 1S).
 *
 * @param s Null-terminated string (may be nullptr).
 * @return Parsed @ref battery_type_t (safe fallback).
 */
battery_type_t battery_type_from_string(const char* s);

/**
 * @brief Return default thresholds for a given battery type.
 *
 * Default values are chosen conservatively for embedded logger usage.
 * They can be overridden by upper layers if needed.
 *
 * @param type Battery technology.
 * @return Default thresholds (mV) for that technology.
 */
battery_thresholds_t battery_thresholds_default(battery_type_t type);

/**
 * @brief Classify a VBAT sample according to thresholds.
 *
 * - If @p vbat_mv is negative => INVALID
 * - If @p vbat_mv < low_crit_mv => CRITICAL
 * - Else if @p vbat_mv < low_warn_mv => WARNING
 * - Else if @p vbat_mv > high_crit_mv => CRITICAL
 * - Else => NORMAL
 *
 * @param vbat_mv Battery voltage sample (mV). May be negative (driver error code).
 * @param thr Thresholds in mV.
 * @return Battery level classification.
 */
battery_level_t battery_classify_mv(int32_t vbat_mv, const battery_thresholds_t& thr);

/**
 * @brief Read the battery voltage from an ADC pin.
 *
 * The function reads the raw ADC code from the specified pin, converts it to millivolts using the provided ADC reference and maximum code, and
 * applies a scaling factor (e.g. for resistor dividers).
 *
 * @param pin ADC pin to read (Arduino pin number).
 * @param cfg ADC conversion configuration (reference voltage, max code, scaling ratio).
 * @return Battery voltage in millivolts (mV), or 0 if an error occurs (e.g. invalid config).
 */
int32_t read_battery_voltage(uint32_t pin, battery_adc_config_t cfg = {});

/**
 * @brief Check if the battery voltage is within plausible bounds.
 *
 * @param vbat_mv Battery voltage in millivolts (mV).
 * @param plausible_min_mv Minimum plausible voltage (mV).
 * @param plausible_max_mv Maximum plausible voltage (mV).
 * @return true if the voltage is plausible, false otherwise.
 */
bool check_battery_voltage_plausibility(int32_t vbat_mv, uint16_t plausible_min_mv, uint16_t plausible_max_mv);

/** @} */  // end of Battery group

#endif  // BATTERY_H
