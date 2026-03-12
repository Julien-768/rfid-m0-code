/**
 * @file sensors_hal.h
 * @brief Generic Hardware Abstraction Layer (HAL) for sensor initialization
 *        and runtime acquisition.
 *
 * This module implements a fully generic mechanism to:
 * - Initialize any set of sensors through a unified interface.
 * - Read all active sensors into an application-defined context structure.
 *
 * The application provides:
 * - A table of @ref SensorSpec entries describing each sensor.
 * - A configuration flag for each sensor (`bool* enabled`).
 * - Initialization and acquisition callbacks.
 *
 * @section example_usage Example
 * @code
 *   static SensorSpec sensors[] = {
 *       { "AS7341",  &config.enable_light1, initAS7341,  readAS7341IntoContext },
 *       { "TSL2591", &config.enable_light2, initTSL2591, readTSL2591IntoContext },
 *       { "VBAT",    &config.enable_vbat,   initVBAT,    readVBATIntoContext   }
 *   };
 *
 *   SensorsHAL_InitForDeploy(sensors, ARRAY_LEN(sensors));
 *
 *   SensorFrame frame;
 *   readAllSensorsGeneric(sensors, ARRAY_LEN(sensors), &frame);
 * @endcode
 *
 * This module intentionally contains **no project-specific logic**.
 * Logging relies exclusively on the LOG_* macros from log.h.
 *
 * @ingroup HalLayer
 */

#pragma once
#include <Arduino.h>

/**
 * @brief One generic sensor entry handled by the HAL.
 *
 * Each entry defines:
 * - @c name    : Human-readable name used for logging.
 * - @c enabled : Pointer to the configuration flag that activates the sensor.
 * - @c initFn  : Initialization callback (may be @c nullptr).
 * - @c readFn  : Acquisition callback writing results into the provided context
 *                (may be @c nullptr for sensors with no runtime data).
 *
 * The application defines the "context" structure (typically @c SensorFrame).
 * SensorSpec remains intentionally minimal so it can be reused in any project.
 */
struct SensorSpec
{
    const char* name;               ///< Display name used in logs.
    bool* enabled;                  ///< Pointer to activation flag (may be nullptr).
    bool (*initFn)();               ///< Initialization callback. Returns true on success.
    void (*readFn)(void* context);  ///< Runtime read callback writing into @p context.
};

/**
 * @brief Initialize all sensors marked as enabled in the given table.
 *
 * For each entry:
 * - If @c enabled is nullptr → the entry is skipped.
 * - If @c *enabled == false → logged as disabled, skipped.
 * - If @c *enabled == true:
 *     - If @c initFn is nullptr → considered a no-op and treated as success.
 *     - If @c initFn fails → logged as ERROR and @c *enabled is set to false.
 *     - On success → logs "<sensor> initialized successfully".
 *
 * This function performs *only* initialization, not acquisition.
 *
 * @param table Pointer to the first element of a SensorSpec array.
 * @param count Number of elements in @p table.
 */
void SensorsHAL_InitForDeploy(SensorSpec* table, size_t count);

/**
 * @brief Read all active sensors into an application-defined context object.
 *
 * For each enabled sensor:
 * - If @c readFn is not null → invoked with @p context.
 * - If @c readFn is null → the sensor simply produces no runtime data.
 *
 * The structure pointed to by @p context is fully application-defined
 * (e.g., a @c SensorFrame struct).
 *
 * @param specs   Sensor specification array.
 * @param count   Number of entries in @p specs.
 * @param context Opaque pointer passed to each @c readFn callback.
 */
void readAllSensorsGeneric(SensorSpec* specs, size_t count, void* context);
