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
#include <string.h>  // strcmp
#include <unordered_map>

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
    if (s == nullptr || s[0] == '\0') return battery_type_t::battery_lipo_1s;

    static const std::unordered_map<std::string, battery_type_t> mapping = {
        {"lipo_1s", battery_type_t::battery_lipo_1s},
        {"liion_1s", battery_type_t::battery_liion_1s},
        {"lifepo4_1s", battery_type_t::battery_lifepo4_1s},
        {"lead_12v", battery_type_t::battery_lead_12v},
    };

    auto it = mapping.find(s);
    return (it != mapping.end()) ? it->second : battery_type_t::battery_lipo_1s;
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

    switch (type)
        {
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

            default:
                // Safe fallback
                t.low_warn_mv  = 3600u;
                t.low_crit_mv  = 3300u;
                t.high_crit_mv = 4400u;
                return t;
        }
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
battery_level_t battery_classify_mv(int32_t vbat_mv, const battery_thresholds_t& thr) {
    if (vbat_mv < 0)
        {
            return battery_level_t::battery_invalid;
    }

    if (thr.high_crit_mv != 0 && vbat_mv > thr.high_crit_mv)
        {
            return battery_level_t::battery_critical_high;
    }

    if (thr.low_crit_mv != 0 && vbat_mv < thr.low_crit_mv)
        {
            return battery_level_t::battery_critical_low;
    }

    if (thr.low_warn_mv != 0 && vbat_mv < thr.low_warn_mv)
        {
            return battery_level_t::battery_warning_low;
    }

    return battery_level_t::battery_normal;
}

bool battery_init() {
    // Configuration de l'ADC (optionnel mais recommandé pour des mesures précises)
    analogReadResolution(12);     // Passe en 12 bits (0-4095) si nécessaire
    analogReference(AR_DEFAULT);  // Utilise la référence par défaut (3.3V)

    // Si vous utilisez une broche spécifique, vous pouvez aussi la configurer en entrée (optionnel)
    pinMode(A0, INPUT);
    return true;
}

/**
 * @brief Read the battery voltage from an ADC pin.
 *
 * @param pin ADC pin to read.
 * @param ratio Scaling factor (e.g. 2.0f for a /2 voltage divider).
 * @param adc_ref_mv ADC reference voltage in millivolts (mV).
 * @param adc_max Maximum ADC value (default: 1023 for 10-bit ADC).
 * @return Battery voltage in millivolts (mV), or 0 if an error occurs.
 */
int32_t read_battery_voltage(uint32_t pin, battery_adc_config_t cfg) {
    if (cfg.ratio <= 0.0f || cfg.adc_ref_mv == 0u || cfg.adc_max == 0u)
        {
            return 0;
    }

    const uint32_t raw    = (uint32_t)analogRead(pin);
    const float adc_mv_f  = ((float)raw * (float)cfg.adc_ref_mv) / (float)cfg.adc_max;
    const float vbat_mv_f = adc_mv_f * cfg.ratio;

    return (int32_t)roundf(vbat_mv_f);
}

/**
 * @brief Check if the battery voltage is within plausible bounds.
 *
 * @param vbat_mv Battery voltage in millivolts (mV).
 * @param plausible_min_mv Minimum plausible voltage (mV).
 * @param plausible_max_mv Maximum plausible voltage (mV).
 * @return true if the voltage is plausible, false otherwise.
 */
bool check_battery_voltage_plausibility(int32_t vbat_mv, uint16_t plausible_min_mv, uint16_t plausible_max_mv) {
    if (plausible_min_mv == 0u || plausible_max_mv == 0u)
        {
            return true;  // Check disabled
    }

    if (plausible_min_mv >= plausible_max_mv)
        {
            return false;  // Invalid configuration
    }

    if (vbat_mv < (int32_t)plausible_min_mv || vbat_mv > (int32_t)plausible_max_mv)
        {
            return false;  // Out of range
    }

    return true;  // OK
}

/** @} */  // end of Battery group
