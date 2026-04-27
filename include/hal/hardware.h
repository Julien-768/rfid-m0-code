/**
 * @file hardware.h
 * @brief Centralized hardware pin mapping for the Feather M0 Adalogger project.
 */

/**
 * @section platform_info Platform Information
 * - Platform: Adafruit Feather M0 (ATSAMD21G18)
 * - MCU: ARM Cortex-M0+ @ 48 MHz
 * - Framework: Arduino (SAMD core)
 * - Logic Level: 3.3V
 */

#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Board selection
// -----------------------------------------------------------------------------

#if !defined(RFID_DOOR) && !defined(FEATHER_M0_ADALOGGER)
#define FEATHER_M0_ADALOGGER
#endif

#define PIN_PR_2 A0   // IR receiver 2
#define PIN_VBAT A1   // Battery voltage measurement
#define PIN_PW_SW A2  // Power switch input
#define PIN_PW_EN A3  // Low battery power relay
#if (LOG_SERIAL_OUTPUT == LOG_ALT_SERIAL)
#define SerialAlt_TX A4  // Alternate serial TX pin (D6 / PA20)
#endif
#define PIN_PWR_5V A5  // 5V rail relay

#define PIN_ERROR LED_BUILTIN  // Error indicator
#define PIN_DET_EXT 12         // FTDI detection
#define PIN_PWM_IR 11          // 36 kHz IR output
#define PIN_PR_1 10            // IR receiver 1
#define PIN_BUZZER_LED 9       // Buzzer or LED
#define PIN_LED_SD 8           // SD card activity indicator
#define RTC_INTERRUPT_PIN 6    // DS3231 INT/SQW output
#define PIN_PWR_3V 5           // 3V rail relay
#define PIN_SD_CS 4            // SD card chip select

#define PWR_3V_ACTIVE_HIGH false
#define PWR_5V_ACTIVE_HIGH false
#define PWR_EN_ACTIVE_HIGH true

#if defined(FEATHER_M0_ADALOGGER)
#define PWM_TIMER 2

// -----------------------------------------------------------------------------
// Board-specific pins
// -----------------------------------------------------------------------------

#elif defined(RFID_DOOR)
#define PIN_PWM_IR 9  // 36 kHz IR output
#define PWM_TIMER 1

#else
#error "Unsupported hardware configuration. Define RFID_DOOR or FEATHER_M0_ADALOGGER."
#endif

// -----------------------------------------------------------------------------
// SD Control
// -----------------------------------------------------------------------------
