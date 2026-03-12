/**
 * @file tsl2591_sensor.h
 * @brief Interface definitions and data structures for the TSL2591 light sensor module.
 *
 * This header declares initialization, data acquisition, and CSV formatting
 * functions for the Adafruit TSL2591 ambient light sensor.
 * It is designed to integrate seamlessly with the Moonraker logger architecture
 * (similar to the AS7341 sensor module).
 *
 * The TSL2591 provides high dynamic range ambient light measurement using
 * dual photodiodes (full-spectrum and infrared) and programmable gain
 * and integration time settings.
 */

#pragma once

#include <Arduino.h>
#include <Adafruit_TSL2591.h>

/**
 * @struct TSL2591Reading
 * @brief Structure to store a complete TSL2591 measurement.
 *
 * The reading includes calculated lux, raw full-spectrum ADC count,
 * and raw infrared (IR) ADC count.
 *
 * Example usage:
 * @code
 * TSL2591Reading r = readTSL2591();
 * Serial.println(r.lux);
 * @endcode
 */
struct TSL2591Reading
{
    float lux;      ///< Calculated ambient light level in lux.
    uint16_t full;  ///< Raw full-spectrum ADC channel count.
    uint16_t ir;    ///< Raw infrared ADC channel count.
};

/**
 * @brief Initializes the TSL2591 sensor.
 *
 * This function sets the gain and integration time and automatically
 * logs the hardware Device ID to the system log and updates the
 * @ref assembly.cfg configuration file.
 *
 * @return true if the sensor was initialized successfully, false otherwise.
 */
bool initTSL2591();

/**
 * @brief Reads the TSL2591 hardware Device ID and updates assembly configuration.
 *
 * Queries the I²C register 0x12 to identify the connected TSL2591 sensor.
 * The identifier is logged and stored in the configuration as
 * `"uid_light_sensor2": "TSL2591_0xXX"`.
 */
void readTSL2591DeviceID();

/**
 * @brief Reads a single measurement from the TSL2591 sensor.
 *
 * This function enables the device, waits for the integration cycle
 * to complete, then reads both full-spectrum and IR channels.
 * The lux value is computed using the Adafruit library helper.
 *
 * @return A @ref TSL2591Reading structure containing lux, full, and IR values.
 */
TSL2591Reading readTSL2591();
