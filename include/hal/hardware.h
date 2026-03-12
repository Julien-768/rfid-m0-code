/**
 * @file hardware.h
 * @brief Centralized hardware pin mapping for Feather M0 Adalogger project.
 *
 * This file defines all GPIO pin assignments and hardware constants used
 * across the project. Keeping these definitions in one place makes it easier
 * to adapt the firmware to changes in wiring or hardware configuration.
 */

#pragma once

#include <Arduino.h>

/**
 * @def PIN_LED_SD
 * @brief GPIO pin used to indicate SD card activity.
 *
 * Set to high when writing to the SD card, low otherwise.
 * This helps visualize SD operations during debugging.
 */
constexpr uint8_t PIN_LED_SD = 8;

/**
 * @def PIN_SD_CS
 * @brief Chip Select (CS) pin for the SD card SPI interface.
 *
 * Connects to the SD card module's CS pin to enable/disable communication.
 */
constexpr uint8_t PIN_SD_CS = 4;

/**
 * @def RTC_INTERRUPT_PIN
 * @brief Pin connected to the INT/SQW output of the DS3231 RTC.
 *
 * Used to wake the Feather M0 from standby sleep when the RTC alarm triggers.
 */
constexpr uint8_t RTC_INTERRUPT_PIN = 10;
