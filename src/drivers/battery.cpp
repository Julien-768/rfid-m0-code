/**
 * @file battery.cpp
 * @defgroup Battery Battery (Driver)
 * @ingroup Drivers
 * @brief Battery technology model + ADC→mV conversion (policy-free).
 *
 * This driver is intentionally **policy-free** and **reusable**.
 *
 * It provides:
 * - Battery technology model (type + default thresholds)
 * - Classification of a measured VBAT value (NORMAL/WARNING/CRITICAL/INVALID)
 * - ADC → mV conversion from `analogRead()` with optional divider ratio
 * - Optional plausibility checking (sanity range)
 *
 * It does NOT provide:
 * - call cadence (tick)
 * - logs / telemetry
 * - shutdown / END-OF-LIFE decisions
 * - error handler calls
 *
 * ## Unit convention
 * All voltages handled by this driver are expressed in **millivolts (mV)**.
 *
 * @see battery.h
 * @{
 */

#include "battery.h"
#include <Arduino.h>

namespace {
uint8_t adc_resolution_bits_from_max(uint16_t adc_max) {
    uint32_t levels = static_cast<uint32_t>(adc_max) + 1u;
    uint8_t bits    = 0u;

    while (levels > 1u && (levels % 2u) == 0u) {
        levels /= 2u;
        ++bits;
    }

    return (levels == 1u) ? bits : 0u;
}

}  // namespace

// -----------------------------------------------------------------------------
// Battery technology model (type + default thresholds)
// -----------------------------------------------------------------------------

/**
 * @brief Convert a configuration string into a @ref battery_type_t.
 *
 * Supported strings (case-sensitive):
 * - `"lipo_1s"`
 * - `"liion_1s"`
 * - `"lifepo4_1s"` (defined; thresholds may be placeholders)
 * - `"lead_12v"`   (defined; thresholds may be placeholders)
 *
 * Unknown or empty strings fall back to a safe default.
 *
 * @param s Null-terminated string (may be nullptr).
 * @return Battery type enum (safe fallback on unknown input).
 */
battery_type_t battery_type_from_string(const char* s) {
    if (s == nullptr || s[0] == '\0') return battery_type_t::battery_unknown;

    if (strcmp(s, "lipo_1s") == 0) return battery_type_t::battery_lipo_1s;
    if (strcmp(s, "liion_1s") == 0) return battery_type_t::battery_liion_1s;
    if (strcmp(s, "lifepo4_1s") == 0) return battery_type_t::battery_lifepo4_1s;
    if (strcmp(s, "lead_12v") == 0) return battery_type_t::battery_lead_12v;

    return battery_type_t::battery_lipo_1s;
}

/**
 * @brief Return default thresholds for a given battery technology.
 *
 * Defaults are expressed in millivolts (mV).
 *
 * The returned thresholds are used by higher layers to classify VBAT as:
 * - NORMAL
 * - WARNING (low battery)
 * - CRITICAL (low battery or unexpected high voltage)
 *
 * @param type Battery technology/type.
 * @return Default thresholds (mV) for that type.
 */
battery_thresholds_t battery_thresholds_default(battery_type_t type) {
    battery_thresholds_t t{};

    switch (type) {
        case battery_type_t::battery_lipo_1s:
            t.low_warn_mv  = 3600u;
            t.low_crit_mv  = 3300u;
            t.high_crit_mv = 4400u;
            return t;

        case battery_type_t::battery_liion_1s:
            t.low_warn_mv  = 3500u;
            t.low_crit_mv  = 3200u;
            t.high_crit_mv = 4400u;
            return t;

        case battery_type_t::battery_lifepo4_1s:
            t.low_warn_mv  = 3200u;
            t.low_crit_mv  = 3000u;
            t.high_crit_mv = 3800u;
            return t;

        case battery_type_t::battery_lead_12v:
            t.low_warn_mv  = 11800u;
            t.low_crit_mv  = 11400u;
            t.high_crit_mv = 15000u;
            return t;

        case battery_type_t::battery_unknown:
        default:
            // Safe fallback
            t.low_warn_mv  = 3600u;
            t.low_crit_mv  = 3300u;
            t.high_crit_mv = 4400u;
            return t;
    }
}

bool battery_init(const battery_hw_config_t& hw_cfg) {
    const uint8_t resolution_bits = adc_resolution_bits_from_max(hw_cfg.adc_cfg.adc_max);

    if (resolution_bits != 0u) {
        analogReadResolution(resolution_bits);
    }

    pinMode(hw_cfg.pin, INPUT);

    // ⚠️ volontairement conservé minimal (pas de changement de ref ADC)
    return true;
}

/**
 * @brief Read the battery voltage from an ADC pin.
 *
 * @param pin ADC pin to read.
 * @param cfg ADC configuration (reference voltage, max value, optional divider ratio).
 * @return Battery voltage in millivolts (mV), or -1 if an error occurs.
 */
int32_t read_battery_voltage(uint32_t pin, const battery_adc_config_t& cfg) {
    if (cfg.ratio <= 0.0f || cfg.adc_ref_mv == 0u || cfg.adc_max == 0u) {
        return -1;  // negative value on error
    }

    const uint32_t raw = analogRead(pin);

    if (raw > cfg.adc_max) {
        return -1;  // invalid ADC reading/config mismatch
    }

    const float mv = (static_cast<float>(raw) / static_cast<float>(cfg.adc_max)) *
                     static_cast<float>(cfg.adc_ref_mv) * cfg.ratio;

    // round to nearest mV
    return static_cast<int32_t>(mv + 0.5f);
}

/**
 * @brief Check if the battery voltage is within plausible bounds.
 *
 * @param vbat_mv Battery voltage in millivolts (mV).
 * @param plausible_min_mv Minimum plausible voltage (mV).
 * @param plausible_max_mv Maximum plausible voltage (mV).
 * @return true if the voltage is plausible, false otherwise
 */
bool check_battery_voltage_plausibility(int32_t mv, uint16_t min_mv, uint16_t max_mv) {
    if (mv < 0) {
        return false;
    }

    const bool min_enabled = (min_mv != 0u);
    const bool max_enabled = (max_mv != 0u);

    if (min_enabled && mv < static_cast<int32_t>(min_mv)) {
        return false;
    }

    if (max_enabled && mv > static_cast<int32_t>(max_mv)) {
        return false;
    }

    return true;
}

/**
 * @brief Classify a battery voltage in mV against thresholds.
 *
 * Classification rules:
 * - If @p vbat_mv is negative => INVALID
 * - If high critical threshold is non-zero and @p vbat_mv > high_crit => CRITICAL
 * - If @p vbat_mv < low_crit => CRITICAL
 * - If @p vbat_mv < low_warn => WARNING
 * - Else => NORMAL
 *
 * @param vbat_mv Battery voltage in millivolts (mV), may be negative (driver error).
 * @param thr Thresholds used for classification.
 * @return Battery level classification.
 */
battery_state_t battery_classify_mv(int32_t mv, const battery_thresholds_t& thr) {
    if (mv < 0) {
        return battery_invalid;
    }

    if (thr.high_crit_mv != 0u && mv > static_cast<int32_t>(thr.high_crit_mv)) {
        return battery_critical_high;
    }

    if (thr.low_crit_mv != 0u && mv < static_cast<int32_t>(thr.low_crit_mv)) {
        return battery_critical_low;
    }

    if (thr.low_warn_mv != 0u && mv < static_cast<int32_t>(thr.low_warn_mv)) {
        return battery_warning_low;
    }

    return battery_normal;
}
/** @} */  // end of Battery group
