/**
 * @file battery_service.cpp
 * @defgroup BatteryService Battery Service
 * @ingroup SystemModules
 * @brief Battery orchestration layer (tick/logs/actions).
 *
 * This module implements the **battery policy orchestration** of the firmware.
 * It sits above the reusable battery driver (`drivers/battery.*`) and is
 * responsible for:
 * - call cadence (periodic checks)
 * - logging and telemetry
 * - triggering system actions on critical battery state (error handler)
 *
 * The underlying driver remains **policy-free** and **reusable**:
 * - ADC -> mV conversion
 * - battery technology model (type + thresholds)
 * - classification (NORMAL/WARNING/CRITICAL/INVALID)
 * - no logs, no shutdown, no error handler calls
 *
 * ## Unit convention
 * All voltages are expressed in **millivolts (mV)**.
 *
 *
 * @note The filtering stage is optional but recommended for noisy ADC environments.
 *
 * It significantly improves measurement stability and prevents false
 * battery warnings due to transient spikes.
 *
 * @see core/battery_service.h
 * @see drivers/battery.h
 * @see core/error_handler.h
 * @see core/log.h
 * @{
 */

#include "battery_service.h"
#include "log.h"

extern battery_service_config_t batt_serv_cfg{};

static battery_hw_config_t batt_hw_cfg{};
static battery_policy_config_t batt_policy_cfg{};
static battery_filter_config_t batt_filter_cfg{};
static battery_filter_state_t batt_filter_state{};

static bool batt_available = false;

bool battery_is_available() {
    return batt_available;
}

void battery_set_available(bool available) {
    batt_available = available;
}

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------

/**
 * @brief Active driver thresholds (technology model).
 *
 * Populated by @ref battery_service_apply_type_string() and may be overridden by
 */
static battery_thresholds_t batt_thresholds_active =
    battery_thresholds_default(battery_type_t::battery_lipo_1s);
// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------

/**
 * @brief Compute the median of three values.
 *
 * This function is used as a lightweight and robust spike filter.
 * It rejects single-sample outliers (e.g. ADC glitches).
 *
 * @param a First sample (mV)
 * @param b Second sample (mV)
 * @param c Third sample (mV)
 * @return Median value (mV)
 */
static int32_t median3(int32_t a, int32_t b, int32_t c) {
    if (a > b) {
        int32_t t = a;
        a         = b;
        b         = t;
    }
    if (b > c) {
        int32_t t = b;
        b         = c;
        c         = t;
    }
    if (a > b) {
        int32_t t = a;
        a         = b;
        b         = t;
    }
    return b;
}

/**
 * @brief Clamp a floating-point value to [0, 1].
 *
 * Used to ensure EMA alpha remains in a valid range.
 *
 * @param x Input value
 * @return Clamped value in [0,1]
 */
static float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

/**
 * @brief Update battery filter state with a new sample.
 *
 * Filtering pipeline:
 *   1. Shift raw samples
 *   2. Apply median(3) to remove spikes
 *   3. Apply EMA smoothing
 *   4. Apply deadband (delta threshold)
 *
 * Behavior:
 * - First call initializes the filter state.
 * - Median removes isolated spikes (robust against ADC noise).
 * - EMA smooths gradual variations.
 * - Deadband avoids reporting insignificant changes.
 *
 * @param s Filter state (persistent)
 * @param new_sample_mv New validated sample (mV)
 * @param cfg Filter configuration
 * @return true if filtered value changed significantly, false otherwise
 */
static bool battery_filter_update(battery_filter_state_t& s,
                                  int32_t new_sample_mv,
                                  const battery_filter_config_t& cfg) {
    const float alpha = clamp01(cfg.ema_alpha);

    if (!s.initialized) {
        s.raw_samples[0] = new_sample_mv;
        s.raw_samples[1] = new_sample_mv;
        s.raw_samples[2] = new_sample_mv;

        s.median_mv    = new_sample_mv;
        s.ema_mv       = new_sample_mv;
        s.published_mv = new_sample_mv;
        s.initialized  = true;
        return true;
    }

    // Shift samples
    s.raw_samples[2] = s.raw_samples[1];
    s.raw_samples[1] = s.raw_samples[0];
    s.raw_samples[0] = new_sample_mv;

    // Median filter (spike rejection)
    s.median_mv = median3(s.raw_samples[0], s.raw_samples[1], s.raw_samples[2]);

    // EMA smoothing
    const float ema =
        static_cast<float>(s.ema_mv) + alpha * static_cast<float>(s.median_mv - s.ema_mv);

    s.ema_mv = static_cast<int32_t>(ema + (ema >= 0.0f ? 0.5f : -0.5f));

    // Deadband (change detection)
    if (abs(s.ema_mv - s.published_mv) >= static_cast<int32_t>(cfg.delta_threshold_mv)) {
        s.published_mv = s.ema_mv;
        return true;
    }

    return false;
}

/**
 * @brief Convert a driver classification into service behavior (log + action).
 *
 * The driver does not execute consequences. This function does:
 * - logs (debug/warn/error)
 * - triggers error handling on critical condition
 *
 * Policy rule:
 * - INVALID readings are ignored (return true) to avoid false critical events.
 *
 * @param context Short string used in log messages ("Boot", "Runtime", ...).
 * @param vbat_mv Battery voltage (mV). May be negative if invalid.
 * @return `true` if acceptable (or invalid/ignored), `false` if critical condition detected.
 *
 * @see battery_classify_mv()
 */
bool battery_service_decision(const char* context, int32_t vbat_mv) {
    const battery_state_t level = battery_classify_mv(vbat_mv, batt_thresholds_active);

    switch (level) {
        case battery_invalid:
            LOG_WARN("%s: VBAT invalid reading (%ld)", context, vbat_mv);
            return true;

        case battery_normal:
            LOG_DEBUG("%s: VBAT=%ld mV (NORMAL)", context, vbat_mv);
            return true;

        case battery_warning_low:
            LOG_WARN("%s: low battery (%ld mV) (WARNING)", context, vbat_mv);
            return true;

        case battery_critical_high:
        case battery_critical_low:
            LOG_ERROR("%s: CRITICAL battery (%ld mV)", context, vbat_mv);
            return false;
    }

    // Safe fallback (should not happen)
    return true;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

/**
 * @brief Initialize the battery service with board-level measurement parameters.
 *
 * Call once at boot, after the HAL knows which ADC pin and divider ratio
 * correspond to VBAT.
 *
 * This function also sets a safe default battery model (LiPo 1S defaults) until
 * @ref battery_service_apply_type_string() is called.
 *
 * @param cfg Measurement configuration (pin/ratio/ADC ref/resolution/plausibility).
 * @see battery_service_apply_type_string()
 * @see battery_service_read_vbat_mv()
 */
bool battery_service_init(const battery_service_config_t& cfg) {

    batt_hw_cfg     = cfg.hw;      // copy for service-level storage (e.g. for periodic checks)
    batt_policy_cfg = cfg.policy;  // copy for service-level storage (e.g. for periodic checks)
    batt_filter_cfg =
        cfg.filter;  // copy for service-level storage (filter parameters could be made dynamic if needed)
    batt_filter_state = {};  // reset filter state at init

    batt_thresholds_active = battery_thresholds_default(battery_type_t::battery_lipo_1s);

    LOG_DEBUG("Battery_hw_cfg: pin=%lu ratio=%.3f adc_ref=%umV adc_max=%u",
              batt_hw_cfg.pin,
              batt_hw_cfg.adc_cfg.ratio,
              batt_hw_cfg.adc_cfg.adc_ref_mv,
              batt_hw_cfg.adc_cfg.adc_max);

    LOG_DEBUG("Battery model defaults: low_warn=%umV low_crit=%umV high_crit=%umV",
              batt_thresholds_active.low_warn_mv,
              batt_thresholds_active.low_crit_mv,
              batt_thresholds_active.high_crit_mv);

    LOG_DEBUG("Battery filter config: ema_alpha=%.3f delta_threshold=%u mV",
              batt_filter_cfg.ema_alpha,
              batt_filter_cfg.delta_threshold_mv);

    return battery_init(batt_hw_cfg);
}

/**
 * @brief Apply battery model defaults based on a battery type string.
 *
 * Parses the provided string into a driver battery type and loads default
 * thresholds for that technology.
 *
 * Unknown/empty strings fall back to a safe default (LiPo 1S).
 *
 * @param battery_type Battery type string (e.g. "lipo_1s", "liion_1s").
 *
 * @see battery_type_from_string()
 * @see battery_thresholds_default()
 */
battery_thresholds_t battery_service_apply_type_string(const String& battery_type) {
    const char* req = battery_type.length() ? battery_type.c_str() : "lipo_1s";
    LOG_DEBUG("Applying battery type from string: '%s'", req);

    const battery_type_t type = battery_type_from_string(req);
    batt_thresholds_active    = battery_thresholds_default(type);

    LOG_DEBUG("Battery model details: type=%d", static_cast<int>(type));
    LOG_DEBUG("\tBattery model thresholds: low_warn=%umV", batt_thresholds_active.low_warn_mv);
    LOG_DEBUG("\tBattery model thresholds: low_crit=%umV", batt_thresholds_active.low_crit_mv);
    LOG_DEBUG("\tBattery model thresholds: high_crit=%umV", batt_thresholds_active.high_crit_mv);

    return batt_thresholds_active;
}

/**
 * @brief Read battery voltage with filtering applied.
 *
 * Processing steps:
 *  1. ADC conversion (driver)
 *  2. Plausibility check (service policy)
 *  3. Median + EMA filtering (service)
 *
 * @return Filtered battery voltage in mV
 *         -1 = ADC/config error
 *         -2 = plausibility error
 */
bool battery_service_read_vbat_filtered_mv(int32_t& vbat_mv, bool& changed) {
    int32_t v = read_battery_voltage(batt_hw_cfg.pin, batt_hw_cfg.adc_cfg);

    if (v < 0) {
        vbat_mv = v;
        changed = false;
        return false;
    }

    if (!check_battery_voltage_plausibility(v,
                                            batt_policy_cfg.plausible_min_mv,
                                            batt_policy_cfg.plausible_max_mv)) {
        vbat_mv = -2;
        changed = false;
        return false;
    }

    changed = battery_filter_update(batt_filter_state, v, batt_filter_cfg);

    vbat_mv = batt_filter_state.ema_mv;

    LOG_DEBUG("VBAT raw=%ld median=%ld ema=%ld changed=%d",
              v,
              batt_filter_state.median_mv,
              batt_filter_state.ema_mv,
              (int)changed);

    return true;
}

/**
 * @brief Periodic battery check with call-rate reduction.
 *
 * Performs the runtime check only every @p period calls.
 *
 * @param vbat_mv Latest VBAT reading (mV). May be negative if invalid.
 * @param counter Persistent counter maintained by the caller.
 * @param period Number of calls between two evaluations (must be > 0).
 * @return `true` if acceptable (or invalid/ignored), `false` if critical condition detected.
 *
 * @see  evaluate_and_act()
 */
bool battery_service_periodic_check(int32_t vbat_mv, uint8_t& counter, uint8_t period) {
    if (period == 0u) period = 1u;

    if (++counter < period) return true;
    counter = 0u;

    return battery_service_decision("Runtime", vbat_mv);
}

/** @} */  // end of BatteryService group
