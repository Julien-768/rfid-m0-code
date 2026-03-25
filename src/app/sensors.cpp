/**
 * @file sensors.cpp
 * @brief High-level sensor aggregation layer for Moonraker.
 *
 * This module provides:
 * - The Moonraker-specific `SensorSpec` table (AS7341, TSL2591, VBAT).
 * - A unified acquisition routine @ref readAllSensors() that fills a @ref SensorFrame.
 * - A logging helper @ref logSensorFrame() that writes one measurement per line.
 *
 * Low-level details are delegated to:
 * - sensors_hal.*         (generic SensorSpec initialization and acquisition dispatch)
 * - as7341_sensor.*       (AS7341 driver + @ref AS7341Reading)
 * - TSL2591_sensor.*      (TSL2591 driver + @ref TSL2591Reading)
 * - battery.*             (battery voltage acquisition)
 *
 * @note This file intentionally focuses on **composition** and **formatting**:
 *       sensor-specific electrical/I2C details belong to their respective drivers.
 */

#include "sd_manager.h"
#include "config.h"
#include "sensors_hal.h"
#include "sensors_internal.h"
#include "log.h"
#include "battery.h"
#include "battery_service.h"
#include <RTClib.h>
#include "sensors.h"

/* ========================================================================= */
/*  HAL acquisition callbacks                                                */
/* ========================================================================= */

// /**
//  * @brief HAL read callback: acquire AS7341 channels into a @ref SensorFrame.
//  *
//  * The HAL calls this function when the AS7341 entry is enabled and scheduled
//  * for reading.
//  *
//  * @param ctx Opaque pointer expected to be a `SensorFrame*`.
//  *
//  * @warning The caller must ensure @p ctx points to a valid @ref SensorFrame.
//  */
// static void readAS7341IntoContext(void* ctx) {
//     auto* frame         = static_cast<SensorFrame*>(ctx);
//     frame->as7341       = readAS7341Channels();
//     frame->valid_as7341 = true;
// }

// /**
//  * @brief HAL read callback: acquire TSL2591 values into a @ref SensorFrame.
//  *
//  * @param ctx Opaque pointer expected to be a `SensorFrame*`.
//  *
//  * @warning The caller must ensure @p ctx points to a valid @ref SensorFrame.
//  */
// static void readTSL2591IntoContext(void* ctx) {
//     auto* frame          = static_cast<SensorFrame*>(ctx);
//     frame->tsl2591       = readTSL2591();
//     frame->valid_tsl2591 = true;
// }

/**
 * @brief HAL read callback: acquire battery voltage into a @ref SensorFrame.
 *
 * @param ctx Opaque pointer expected to be a `SensorFrame*`.
 *
 * @warning The caller must ensure @p ctx points to a valid @ref SensorFrame.
 */
static void battery_measurement(void* ctx) {
    auto* frame    = static_cast<SensorFrame*>(ctx);
    frame->vbat_mv = read_battery_voltage(PIN_VBAT);
}

bool battery_initialisation() {
    return battery_service_init();
}

/**
 * @todo Simplify validity handling: consider removing `valid_vbat` and relying
 *       solely on the `enabled` flag for each sensor, and/or reading directly
 *       from per-sensor read functions without separate validity flags.
 */

/* ========================================================================= */
/*  Moonraker sensor table                                                   */
/* ========================================================================= */

/**
 * @brief Moonraker sensor specification table.
 *
 * Each entry binds:
 * - a human-readable name,
 * - an enable flag (typically from @ref config),
 * - an initialization function,
 * - a read callback used by the generic HAL dispatch.
 *
 * @note The actual enable/disable behavior is controlled by the pointed boolean
 *       flags (e.g., `config.enable_light1`).
 */
SensorSpec g_sensors[] = {
    // name      enabled-flag             initFn       readFn
    // {"AS7341", &config.enable_light1, initAS7341, readAS7341IntoContext},
    // {"TSL2591", &config.enable_light2, initTSL2591, readTSL2591IntoContext},
    // TODO battery
    {"VBAT", &config.enable_vbat, battery_initialisation, battery_measurement},
};

/**
 * @brief Number of entries in @ref g_sensors.
 */
const size_t G_SENSOR_COUNT = sizeof(g_sensors) / sizeof(g_sensors[0]);

/* ========================================================================= */
/*  Initialization                                                           */
/* ========================================================================= */

/**
 * @brief Initialize all sensors listed in a `SensorSpec` table for deployment.
 *
 * This function forwards to the generic HAL initialization layer.
 *
 * @param table Pointer to a `SensorSpec` array.
 * @param count Number of entries in @p table.
 *
 * @see SensorsHAL_InitForDeploy()
 */
void Sensors_InitForDeploy(SensorSpec* table, size_t count) {
    SensorsHAL_InitForDeploy(table, count);
}

/* ========================================================================= */
/*  Unified acquisition                                                      */
/* ========================================================================= */

/**
 * @brief Read all enabled sensors from a `SensorSpec` table.
 *
 * Disabled sensors (e.g., `config.enable_* == false`) will not modify the frame
 * and their `valid_*` flags remain `false`.
 *
 * @param table Pointer to a `SensorSpec` array.
 * @param count Number of entries in @p table.
 * @return A @ref SensorFrame filled with all available measurements.
 *
 * @note The returned frame is default-initialized; all `valid_*` flags start as
 *       `false` and are set by the corresponding callbacks.
 *
 * @see readAllSensorsGeneric()
 */
SensorFrame readAllSensors(SensorSpec* table, size_t count) {
    SensorFrame frame{};  // all valid_* are false by default
    readAllSensorsGeneric(table, count, &frame);
    return frame;
}

/* ========================================================================= */
/*  Logging                                                                  */
/* ========================================================================= */

/**
 * @brief Log all measurements contained in a @ref SensorFrame.
 *
 * Each value is logged as one line via @ref logMeasurement, typically in the
 * data log file:
 *
 * `YYYY-MM-DD HH:MM:SS;SENSOR;value;unit;`
 *
 * - AS7341 → 10 lines: F1..F8, CLEAR, NIR
 * - TSL2591 → 3 lines: LUX, FULL, IR
 * - VBAT → 1 line: battery voltage
 *
 * @param now Timestamp applied to each measurement line.
 * @param f   Sensor frame containing measurements and validity flags.
 *
 * @note Logging is conditional on `f.valid_*` flags. If a sensor is enabled but
 *       fails to read and does not set its flag, it will produce no log lines.
 */
void logSensorFrame(const DateTime& now, const SensorFrame& f) {

    // --- VBAT ---
    if (f.valid_vbat) {
        logMeasurement(now, "VBAT", (float)f.vbat_mv, "V", config.use_buffer);
    }
}
