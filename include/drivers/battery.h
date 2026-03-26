#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

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
 * - Provide helper for plausibility checking (stateless utility)
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
enum class battery_type_t : uint8_t {
    battery_lipo_1s    = 0,   ///< LiPo single-cell.
    battery_liion_1s   = 1,   ///< Li-ion single-cell.
    battery_lifepo4_1s = 2,   ///< LiFePO4 single-cell.
    battery_lead_12v   = 3,   ///< Lead-acid 12V.
    battery_unknown    = 255  ///< Unknown or invalid battery type.
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
typedef struct {
    uint16_t low_warn_mv  = 0;  ///< Warning threshold for low voltage (mV).
    uint16_t low_crit_mv  = 0;  ///< Critical threshold for low voltage (mV).
    uint16_t high_crit_mv = 0;  ///< Critical threshold for high voltage (mV).
} battery_thresholds_t;

/**
 * @brief ADC configuration for battery measurement.
 */
typedef struct {
    float ratio         = 2.0f;  ///< Scaling factor after ADC conversion (default: 2.0f).
    uint16_t adc_ref_mv = 3300;  ///< ADC reference in mV (default: 3300).;
    uint16_t adc_max    = 4095;  ///< Maximum ADC code (default: 4095 for 12-bit on feather m0).
} battery_adc_config_t;

/**
 * @brief Full measurement configuration.
 */

struct battery_hw_config_t {
    uint32_t pin = 0;              ///< ADC pin to read.
    battery_adc_config_t adc_cfg;  ///< ADC conversion parameters.
};

/**
 * @brief Classification of a VBAT sample according to predefined thresholds.
 *
 * The driver only classifies the battery level. It does not log events nor take automatic actions.
 * Use this enum to determine the state of the battery based on voltage or capacity measurements.
 */
typedef enum : uint8_t {
    battery_invalid = 0,    ///< Invalid measurement (e.g. negative voltage).
    battery_normal,         ///< Battery voltage is normal.
    battery_critical_high,  ///< Battery voltage is too high.
    battery_critical_low,   ///< Battery voltage is too low.
    battery_warning_low,    ///< Battery voltage is low (warning level).
} battery_state_t;

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
 * @brief Initialize battery driver.
 */
bool battery_init(const battery_hw_config_t& cfg);

/**
 * @brief Read battery voltage in mV.
 * @return Voltage in mV, negative value on error.
 */
int32_t read_battery_voltage(uint32_t pin, const battery_adc_config_t& cfg);

/**
 * @brief Check plausibility of voltage.
 */
bool check_battery_voltage_plausibility(int32_t mv, uint16_t min_mv, uint16_t max_mv);

/**
 * @brief Classify voltage.
 */
battery_state_t battery_classify_mv(int32_t mv, const battery_thresholds_t& thr);

#endif  // BATTERY_H
