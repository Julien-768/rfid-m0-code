/**
 * @file hardware.h
 * @brief Centralized hardware pin mapping for the Feather M0 Adalogger project.
 */

#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Board selection
// -----------------------------------------------------------------------------

#if !defined(RFID_DOOR) && !defined(FEATHER_M0_ADALOGGER)
#define FEATHER_M0_ADALOGGER
#endif

// -----------------------------------------------------------------------------
// Common pins
// -----------------------------------------------------------------------------

#define PIN_VBAT A2            // Battery voltage measurement
#define PIN_PR_1 5             // IR receiver 1
#define PIN_PR_2 6             // IR receiver 2
#define PIN_PW_SERVO A1        // Servo power relay
#define PIN_TEMP_CS 12         // RTD sensor chip select
#define PIN_DET_EXT 5          // FTDI detection
#define PIN_ERROR LED_BUILTIN  // Error indicator

#define PIN_PW_SW A3   // Power switch input
#define PIN_PW_EN A4   // Low battery power relay
#define PIN_PWR_3V 10  // 3V rail relay
#define PIN_PWR_5V 14  // 5V rail relay

#define PWR_3V_ACTIVE_HIGH false
#define PWR_5V_ACTIVE_HIGH false

// -----------------------------------------------------------------------------
// Board-specific pins
// -----------------------------------------------------------------------------

#if defined(RFID_DOOR)

#define PIN_BUZZER_LED 19  // Buzzer or LED
#define PIN_IR_SEND 9      // 36 kHz IR output
#define PWM_TIMER 1

#elif defined(FEATHER_M0_ADALOGGER)

#define PIN_BUZZER_LED 13  // Buzzer or LED
#define PIN_IR_SEND 11     // 36 kHz IR output
#define PWM_TIMER 2

#else
#error "Unsupported hardware configuration. Define RFID_DOOR or FEATHER_M0_ADALOGGER."
#endif

// -----------------------------------------------------------------------------
// SD card / RTC
// -----------------------------------------------------------------------------

#define PIN_LED_SD 8          // SD card activity indicator
#define PIN_SD_CS 4           // SD card chip select
#define RTC_INTERRUPT_PIN 10  // DS3231 INT/SQW output
