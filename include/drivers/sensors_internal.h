/**
 * @file sensors_internal.h
 * @brief Internal sensor table (g_sensors) declaration.
 *
 * This header is *internal* to the firmware.
 * It exposes the concrete SensorSpec table used by the logger so that
 * high-level application code can explicitly pass it to:
 *
 *  - Sensors_InitForDeploy(SensorSpec* table, size_t count)
 *  - readAllSensors(SensorSpec* table, size_t count)
 *
 * @warning Not intended for generic libraries or external modules.
 */

#pragma once

#include "sensors_hal.h"  // for SensorSpec

/// Logger sensor table (defined in sensors.cpp).
extern SensorSpec g_sensors[];

/// Number of entries in @ref g_sensors.
extern const size_t G_SENSOR_COUNT;
