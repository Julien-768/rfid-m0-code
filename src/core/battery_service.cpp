/**
 * @file battery_service.cpp
 * @defgroup BatteryService Battery Service
 * @ingroup SystemModules
 * @brief Battery orchestration layer for Moonraker (tick/logs/actions).
 *
 * This module implements the **battery policy orchestration** of the firmware.
 * It sits above the reusable battery driver (`drivers/battery.*`) and is
 * responsible for:
 * - call cadence (periodic checks / boot checks)
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
 * @see core/battery_service.h
 * @see drivers/battery.h
 * @see core/error_handler.h
 * @see core/log.h
 * @{
 */

#include "battery_service.h"
#include "error_handler.h"
#include "log.h"
#include "battery.h"

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------

/**
 * @brief Active measurement configuration (driver-level).
 *
 * Set by @ref battery_service_init().
 *
 * @note The service API uses @ref BatteryMeasureConfig, which is converted into
 *       the driver configuration type @ref battery_measure_config_t (including
 *       its nested @ref battery_adc_config_t) and stored here.
 */
static battery_measure_config_t g_measure_cfg{};

/**
 * @brief Active driver thresholds (technology model).
 *
 * Populated by @ref battery_service_apply_type_string() and may be overridden by
 * @ref battery_service_set_thresholds().
 */
static battery_thresholds_t g_thr{};

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------

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
static bool evaluate_and_act(const char* context, int32_t vbat_mv) {
    const battery_level_t level = battery_classify_mv(vbat_mv, g_thr);

    switch (level)
        {
            case battery_level_t::battery_invalid:
                LOG_WARN("%s: VBAT invalid reading (%ld)", context, (long)vbat_mv);
                return true;

            case battery_level_t::battery_normal:
                LOG_DEBUG("%s: VBAT=%ld mV (NORMAL)", context, (long)vbat_mv);
                return true;

            case battery_level_t::battery_warning_low:
                LOG_WARN("%s: low battery (%ld mV) (WARNING)", context, (long)vbat_mv);
                return true;

            case battery_level_t::battery_critical_high:
            case battery_level_t::battery_critical_low:
                LOG_ERROR("%s: CRITICAL battery (%ld mV)", context, (long)vbat_mv);
                error_signal(ERR_BATTERY_CRITICAL, false);
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
 *
 * @see BatteryMeasureConfig
 * @see battery_service_apply_type_string()
 * @see battery_service_read_vbat_mv()
 */
bool battery_service_init(const battery_measure_config_t& cfg) {

    // Safe defaults until the configured type is applied.
    g_thr = battery_thresholds_default(battery_type_t::battery_lipo_1s);

    LOG_INFO("Battery service init: pin=%lu ratio=%.3f adc_ref=%umV adc_max=%u plausible=[%u..%u]mV", (unsigned long)cfg.pin, cfg.adc_cfg.ratio,
             cfg.adc_cfg.adc_ref_mv, cfg.adc_cfg.adc_max, cfg.plausible_min_mv, cfg.plausible_max_mv);
    return battery_init();
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
void battery_service_apply_type_string(const String& battery_type) {
    const char* req           = battery_type.length() ? battery_type.c_str() : "lipo_1s";
    const battery_type_t type = battery_type_from_string(battery_type.c_str());
    g_thr                     = battery_thresholds_default(type);

    LOG_INFO("Battery model applied: requested=%s warn_low=%umV crit_low=%umV crit_high=%umV", req, g_thr.low_warn_mv, g_thr.low_crit_mv,
             g_thr.high_crit_mv);
}

/**
 * @brief Override the active thresholds.
 *
 * Useful for tests or for a configuration path that provides explicit thresholds.
 *
 * @param t Thresholds in millivolts (mV).
 */
void battery_service_set_thresholds(const BatteryThresholds& t) {
    g_thr.low_warn_mv  = t.warn_mv;
    g_thr.low_crit_mv  = t.critical_mv;
    g_thr.high_crit_mv = t.high_crit_mv;

    LOG_INFO("Battery thresholds overridden: warn_low=%umV crit_low=%umV crit_high=%umV", g_thr.low_warn_mv, g_thr.low_crit_mv, g_thr.high_crit_mv);
}

/**
 * @brief Return currently active thresholds.
 *
 * @return Thresholds in millivolts (mV).
 */
BatteryThresholds battery_service_get_thresholds() {
    BatteryThresholds out{};
    out.warn_mv      = g_thr.low_warn_mv;
    out.critical_mv  = g_thr.low_crit_mv;
    out.high_crit_mv = g_thr.high_crit_mv;
    return out;
}

battery_adc_config_t cfg = g_measure_cfg.adc_cfg;
/**
 * @brief Read VBAT once using the configured measurement parameters.
 *
 * Internally calls the driver ADC conversion wrapper.
 *
 * @return Battery voltage in mV, or a negative error code from the driver.
 *
 * @see read_battery_voltage()
 */
int32_t battery_service_read_vbat_mv() {
    return read_battery_voltage(g_measure_cfg.pin, cfg);
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

    return evaluate_and_act("Runtime", vbat_mv);
}

/** @} */  // end of BatteryService group
