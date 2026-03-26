/**
 * @file deploy_mode.cpp
 * @brief Implementation of the DEPLOY runtime state for the Moonraker logger.
 *
 * This module implements the main low-power acquisition loop used during
 * deployment. In DEPLOY mode, the logger:
 *
 * - Sleeps in standby until the RTC alarm triggers.
 * - Wakes up, schedules the next wake-up time.
 * - Ensures that the daily data file exists.
 * - Reads all active sensors into a unified SensorFrame.
 * - Logs the SensorFrame to the SD card via @ref logSensorFrame().
 * - Periodically checks the battery level and may request END-OF-LIFE.
 *
 * @see runDeployState()
 * @see readAllSensors()
 * @see logSensorFrame()
 * @see rtc_scheduleNextWake()
 */

#include "deploy_mode.h"
#include "sensors.h"
#include "sensors_internal.h"
#include "sd_manager.h"
#include "rtc.h"
#include "battery.h"
#include "battery_service.h"
#include "config.h"
#include "log.h"
#include "error_handler.h"
#include <ArduinoLowPower.h>
#include "utils.h"
#include "assembly.h"
#include "hardware.h"
#include "ir_pwm.h"

// External references to global objects and configuration
extern ir_pwm
    ir_driver;  // not as good as using a reference `run_boot_sequence(ir_pwm& driver);` but will do the job for now

// Event flags for DEPLOY state
enum deploy_event : uint8_t {
    DEPLOY_EVT_NONE           = 0,
    DEPLOY_EVT_RTC_WAKE       = 1 << 0,
    DEPLOY_EVT_SENSOR_AS7341  = 1 << 1,
    DEPLOY_EVT_SENSOR_TSL2591 = 1 << 2,
    DEPLOY_EVT_SENSOR_IR1     = 1 << 3,
    DEPLOY_EVT_SENSOR_IR2     = 1 << 4,
};

// Global event flags set by ISRs and checked in the main loop.
volatile uint8_t g_deploy_events = DEPLOY_EVT_NONE;

// Global counters for rtc
volatile uint32_t g_rtc_wake_count = 0;

// IR event counters
volatile uint32_t g_ir1_count = 0;
volatile uint32_t g_ir2_count = 0;

// Last trigger timestamps (in us)
volatile uint32_t g_ir1_last_ts = 0;
volatile uint32_t g_ir2_last_ts = 0;

// Minimum delay between two valid events (us)
constexpr uint32_t ir_debounce_us = 50000;

// ===== Interrupt Service Routines =====

void callback_rtc() {
    g_deploy_events |= DEPLOY_EVT_RTC_WAKE;
    g_rtc_wake_count++;
}

void callback_ir1(uint8_t state) {
    uint32_t now = micros();

    if ((now - g_ir1_last_ts) < ir_debounce_us) return;

    g_ir1_last_ts = now;

    g_ir1_count++;
    g_deploy_events |= DEPLOY_EVT_SENSOR_IR1;
}

void callback_ir2(uint8_t state) {
    uint32_t now = micros();

    if ((now - g_ir2_last_ts) < ir_debounce_us) return;

    g_ir2_last_ts = now;

    g_ir2_count++;
    g_deploy_events |= DEPLOY_EVT_SENSOR_IR2;
}

void callback_as7341() {
    g_deploy_events |= DEPLOY_EVT_SENSOR_AS7341;
}

void callback_tsl2591() {
    g_deploy_events |= DEPLOY_EVT_SENSOR_TSL2591;
}

bool init_step        = false;
uint32_t vbat_counter = 0;
uint32_t rtc_period   = 0;

/**
 * @brief Main DEPLOY state handler.
 *
 * This function implements one full iteration of the DEPLOY state.
 *
 * @param state Reference to the current system state. May be set to
 *              @ref STATE_ENDOFLIFE by the battery check or other subsystems.
 */
void run_deploy_state(SystemState& state) {
    if (!init_step) {
        rtc_period = config.acquisition_interval_s;
        // Bypass drivers interrupts
        ir_driver.set_callback_sensor_1(callback_ir1);
        ir_driver.set_callback_sensor_2(callback_ir2);
        rtc_set_alarm_callback(callback_rtc);
        rtc_clear_and_set_alarm(rtc().now(), rtc_period);
        init_step = true;
    }

    uint8_t events          = DEPLOY_EVT_NONE;
    uint32_t rtc_wake_count = 0;
    uint32_t ir1_count      = 0;
    uint32_t ir2_count      = 0;
    int32_t vbat_mv         = 0;
    bool changed            = false;

    // Enter low-power sleep; RTC alarm or sensor interrupt will wake the MCU.
    LowPower.sleep();

    noInterrupts();
    events          = g_deploy_events;
    g_deploy_events = DEPLOY_EVT_NONE;

    rtc_wake_count   = g_rtc_wake_count;
    g_rtc_wake_count = 0;

    ir1_count   = g_ir1_count;
    g_ir1_count = 0;

    ir2_count   = g_ir2_count;
    g_ir2_count = 0;
    interrupts();

    // If wake-up was not caused by the RTC alarm or a sensor interrupt, exit early.
    if (events == DEPLOY_EVT_NONE) return;

    if (events & DEPLOY_EVT_RTC_WAKE) {
        // Clear alarm and program next wake-up.
        rtc_clear_and_set_alarm(rtc().now(), rtc_period);
    }

    // ===== Periodic full acquisition (RTC driven) =====
    if (events & DEPLOY_EVT_RTC_WAKE) {

        // Ensure today’s data file exists.
        DateTime now = rtc().now();
        check_and_create_new_daily_file(now);

        // Read all active sensors.
        SensorFrame frame = readAllSensors(g_sensors, G_SENSOR_COUNT);

        // Log all sensor readings (AS7341, TSL2591, VBAT, etc.).
        logSensorFrame(now, frame);

        vbat_counter += rtc_wake_count;

        // Perform battery check every 10 RTC wakes.
        if (vbat_counter >= 10) {
            vbat_counter = 0;
            battery_service_read_vbat_filtered_mv(vbat_mv, changed);
            if (!battery_service_decision("Boot", vbat_mv)) {
                // Handle decision failure
                error_signal(ERR_BATTERY_CRITICAL, false, PIN_ERROR);
                state = STATE_ENDOFLIFE;
                return;
            }
        }
    }

    // ===== Event-driven partial acquisition =====

    if (events & DEPLOY_EVT_SENSOR_AS7341) {
        // Read only AS7341 data (faster, lower power).
        // Example: read spectral channels without full acquisition cycle.
    }

    if (events & DEPLOY_EVT_SENSOR_TSL2591) {
        // Read only TSL2591 data or handle threshold event.
    }

    if (events & DEPLOY_EVT_SENSOR_IR1) {
        // Handle IR1 event(s) using ir1_count
    }

    if (events & DEPLOY_EVT_SENSOR_IR2) {
        // Handle IR2 event(s) using ir2_count
    }
}
