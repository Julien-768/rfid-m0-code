/**
 * @file hardware.cpp
 * @brief Physical hardware instances for the Moonraker device.
 *
 * This module owns all globally unique MCU-level peripherals:
 * - AS7341 spectral sensor
 * - AS7341 automatic gain controller
 * - TSL2591 light sensor
 * - DS3231 RTC
 *
 * These instances are shared across drivers and HAL layers.
 */
