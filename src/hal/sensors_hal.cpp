/**
 * @file sensors_hal.cpp
 * @brief Generic Hardware Abstraction Layer (HAL) for sensor initialization
 *        and runtime acquisition.
 *
 * This module implements platform-agnostic helper routines used to:
 * - Initialize a list of sensors described by @ref SensorSpec.
 * - Read data from all active sensors into an application-defined context.
 *
 * The HAL remains completely generic: it does not depend on any code,
 * drivers, or data structures. All behaviors come from user-provided callbacks
 * in the SensorSpec table.
 *
 * Logging is performed using the LOG_* macros from log.h.
 *
 * @ingroup HalLayer
 */

#include "sensors_hal.h"
#include "log.h"

/* ========================================================================== */
/*  Initialization Routine                                                    */
/* ========================================================================== */

/**
 * @brief Initialize all sensors declared in the provided SensorSpec table.
 *
 * Initialization logic:
 * - If `enabled` is null → entry is skipped.
 * - If `*enabled == false` → sensor is logged as disabled and skipped.
 * - If `initFn == nullptr` → considered a no-op (success).
 * - If `initFn()` returns false:
 *     - Logged as ERROR.
 *     - The configuration flag is set to false to prevent further use.
 * - On success → "`<name> initialized successfully`" is logged.
 *
 * This routine performs **initialization only**, not acquisition.
 *
 * @param specs Pointer to the first element of a SensorSpec array.
 * @param count Number of entries in the array.
 */
void SensorsHAL_InitForDeploy(SensorSpec* specs, size_t count) {
    LOG_DEBUG("Initializing sensors for Deploy mode...");

    for (size_t i = 0; i < count; ++i) {
        auto& s = specs[i];

        // ---- Disabled sensor ------------------------------------------------
        if (!(s.enabled && *s.enabled)) {
            LOG_INFO((String(s.name) + " disabled in configuration").c_str());
            continue;
        }

        // ---- No init function (optional sensors or virtual sensors) ---------
        if (!s.initFn) {
            LOG_INFO((String(s.name) + " has no init function, skipping init").c_str());
            continue;
        }

        // ---- Initialization failed -----------------------------------------
        if (!s.initFn()) {
            LOG_ERROR((String(s.name) + " initialization failed — disabled").c_str());
            *s.enabled = false;  // Prevent further acquisition
            continue;
        }

        // ---- Successful initialization --------------------------------------
        LOG_INFO((String(s.name) + " initialized successfully").c_str());
    }

    LOG_DEBUG("All active sensors initialized");
}

/* ========================================================================== */
/*  Acquisition Routine                                                       */
/* ========================================================================== */

/**
 * @brief Invoke acquisition callbacks for all active sensors.
 *
 * For each entry in @p specs:
 * - If `enabled == nullptr` or `*enabled == false` → skipped.
 * - If `readFn == nullptr` → skipped (sensor produces no runtime data).
 * - Otherwise → `readFn(context)` is called.
 *
 * The function does **not** allocate or manage the context object;
 * ownership is entirely the responsibility of the caller.
 *
 * @param specs   Sensor specification array.
 * @param count   Number of SensorSpec entries.
 * @param context Pointer to an application-defined structure (e.g. SensorFrame).
 */
void readAllSensorsGeneric(SensorSpec* specs, size_t count, void* context) {
    for (size_t i = 0; i < count; ++i) {
        auto& s = specs[i];

        if (!(s.enabled && *s.enabled)) continue;  ///< Sensor disabled
        if (!s.readFn) continue;                   ///< No runtime data

        s.readFn(context);
    }
}
