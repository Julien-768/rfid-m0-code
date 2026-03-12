#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include "drivers/battery.h"
#include "hal/sensors_hal.h"

/**
 * @brief Unified frame holding all sensor readings and validity flags.
 */
struct SensorFrame
{
    // AS7341Reading as7341;
    // bool valid_as7341 = false;

    // TSL2591Reading tsl2591;
    // bool valid_tsl2591 = false;

    int32_t vbat_mv = 0.0f;
    bool valid_vbat = false;
};

/**
 * @brief Read all enabled sensors described in a SensorSpec table.
 *
 * @param table Pointer to the array of SensorSpec.
 * @param count Number of elements in @p table.
 * @return Filled SensorFrame containing all measured values.
 */
SensorFrame readAllSensors(SensorSpec* table, size_t count);

/**
 * @brief Initialize all sensors listed in the SensorSpec table.
 */
void Sensors_InitForDeploy(SensorSpec* table, size_t count);

/**
 * @brief Log all values of a SensorFrame to SD card.
 */
void logSensorFrame(const DateTime& now, const SensorFrame& f);
