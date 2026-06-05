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

#define RFID_DOOR

#if !defined(RFID_DOOR) && !defined(FEATHER_M0_ADALOGGER)
// default to Feather M0 Adalogger if no board is specified
#define FEATHER_M0_ADALOGGER
#endif

#if defined(FEATHER_M0_ADALOGGER)
#define PWM_TIMER 2

#define PIN_PR_2 A0   // IR receiver 2
#define PIN_VBAT A1   // Battery voltage measurement
#define PIN_PW_SW A2  // Power switch input
#define PIN_PW_EN A3  // Low battery power relay
#if (LOG_SERIAL_OUTPUT == LOG_ALT_SERIAL)
#define SerialAlt_TX A4  // Alternate serial TX pin (D6 / PA20)
#endif
#define PIN_PWR_5V A5  // 5V rail relay

#define PIN_PWM_IR 11        // 36 kHz IR output
#define PIN_PR_1 10          // IR receiver 1
#define PIN_BUZZER_LED 9     // Buzzer or LED
#define PIN_LED_SD 8         // SD card activity indicator
#define RTC_INTERRUPT_PIN 6  // DS3231 INT/SQW output
#define PIN_PWR_3V 5         // 3V rail relay
#define PIN_SD_CS 4          // SD card chip select

#define PWR_3V_ACTIVE_HIGH false
#define PWR_5V_ACTIVE_HIGH false
#define PWR_EN_ACTIVE_HIGH true

#elif defined(RFID_DOOR)
// Commit a9097436 authored Oct 6, 2023 by Julien 🦅

#define PWM_TIMER 1        // Timer 1 supports PWM on pins 9 and 10
#define PIN_PWR_5V A0      // 5V rail relay
#define PIN_VBAT A7        // Battery voltage measurement
#define PIN_PW_SW A3       // Power switch input
#define PIN_PW_EN A4       // Low battery power relay
#define PIN_BUZZER_LED A5  // Buzzer or LED

#define PIN_SD_CS 4           // SD card chip select
#define PIN_PR_1 5            // IR receiver 1
#define PIN_PR_2 6            // IR receiver 2
#define PIN_LED_SD 8          // SD card activity indicator
#define PIN_PWM_IR 9          // 36 kHz IR output
#define PIN_PWR_3V 10         // 3V rail relay
#define RTC_INTERRUPT_PIN 11  // DS3231 INT/SQW output // A1 = D15 // formerly SERVO
#if (LOG_SERIAL_OUTPUT == LOG_ALT_SERIAL)
#define SerialAlt_TX 12  // Alternate serial TX pin
#endif

#define PWR_3V_ACTIVE_HIGH false
#define PWR_5V_ACTIVE_HIGH false
#define PWR_EN_ACTIVE_HIGH true

#else
#error "Unsupported hardware configuration. Define RFID_DOOR or FEATHER_M0_ADALOGGER."
#endif
