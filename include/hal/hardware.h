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

#define feather_m0_adalogger

#ifdef RFID_DOOR
#define PIN_VBAT A2        // analog input for battery voltage measurement
#define PIN_BUZZER_LED 19  // output for buzzer or led

#define PIN_PR_1 5     // input for IR receiver 1
#define PIN_PR_2 6     // input for IR receiver 2
#define PIN_IR_SEND 9  // output pwm 36kHz for IR sensor

#define PIN_ERROR LED_BUILTIN  // output for error signaling (LED)

#define PIN_PW_SW A3    // input for power switch
#define PIN_PW_EN A4    // output for power relay low battery
#define PIN_PW_3V 10    // output for power relay of IRs and RTD
#define PIN_PW_RFID 14  // output for power relay of RFID

#define PIN_ENABLED LOW    // Logic level to activate transistor
#define PIN_DISABLED HIGH  // Logic level to disactivate transistor
#define PWM_TIMER 1        // Timer associated to PWM pin 9

#define PIN_SERVO 11     // output pwm for signal servo pin
#define PIN_PW_SERVO A1  // output for power relay of servomotor
#define PIN_TEMP_CS 12   // input for RTD sensor
#endif

#ifdef feather_m0_adalogger
#define PIN_VBAT A2        // analog input for battery voltage measurement
#define PIN_BUZZER_LED 13  // output for buzzer or led

#define PIN_PR_1 5     // input for IR receiver 1
#define PIN_PR_2 6     // input for IR receiver 2
#define PIN_IR_SEND 9  // output pwm 36kHz for IR sensor

#define PIN_ERROR LED_BUILTIN  // output for error signaling (LED)

#define PIN_PW_SW A3    // input for power switch
#define PIN_PW_EN A4    // output for power relay low battery
#define PIN_PW_3V 10    // output for power relay of IRs and RTD
#define PIN_PW_RFID 14  // output for power relay of RFID

#define PIN_ENABLED LOW    // Logic level to activate transistor
#define PIN_DISABLED HIGH  // Logic level to disactivate transistor
#define PWM_TIMER 1        // Timer associated to PWM pin 9

#define PIN_SERVO 11     // output pwm for signal servo pin
#define PIN_PW_SERVO A1  // output for power relay of servomotor
#define PIN_TEMP_CS 12   // input for RTD sensor
#endif

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
