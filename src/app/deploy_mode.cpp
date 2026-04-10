/**
 * @file deploy_mode.cpp
 * @brief Implementation of the DEPLOY runtime state for the logger.
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
static volatile uint8_t g_deploy_events = DEPLOY_EVT_NONE;

// Global counter for RTC wake events
static volatile uint32_t g_rtc_wake_count = 0;

// IR event counters
static volatile uint32_t g_ir1_count = 0;
static volatile uint32_t g_ir2_count = 0;

// Last trigger timestamps (in us)
static volatile uint32_t g_ir1_last_ts = 0;
static volatile uint32_t g_ir2_last_ts = 0;

// Minimum delay between two valid events (us)
constexpr uint32_t IR_DEBOUNCE_US = 50000;

// Counter for periodic battery checks
constexpr uint8_t VBAT_CHECK_INTERVAL = 10;

// Runtime state
static uint32_t vbat_counter = 0;
static uint32_t rtc_period   = 0;

// ===== Interrupt Service Routines =====

static void callback_rtc() {
    g_deploy_events |= DEPLOY_EVT_RTC_WAKE;
    g_rtc_wake_count++;
}

static void callback_ir1(uint8_t /*state*/) {
    uint32_t now = micros();

    if ((now - g_ir1_last_ts) < IR_DEBOUNCE_US) return;

    g_ir1_last_ts = now;

    g_ir1_count++;
    g_deploy_events |= DEPLOY_EVT_SENSOR_IR1;
}

static void callback_ir2(uint8_t /*state*/) {
    uint32_t now = micros();

    if ((now - g_ir2_last_ts) < IR_DEBOUNCE_US) return;

    g_ir2_last_ts = now;

    g_ir2_count++;
    g_deploy_events |= DEPLOY_EVT_SENSOR_IR2;
}

static void callback_as7341() {
    g_deploy_events |= DEPLOY_EVT_SENSOR_AS7341;
}

static void callback_tsl2591() {
    g_deploy_events |= DEPLOY_EVT_SENSOR_TSL2591;
}

/**
 * @brief Initialize DEPLOY mode runtime and callbacks.
 *
 * Must be called once when entering DEPLOY state.
 *
 * @param ir_driver Reference to the IR PWM driver.
 */
void deploy_enter(ir_pwm& ir_driver) {
    noInterrupts();

    // Reset event flags and counters (ISR-related)
    g_deploy_events  = DEPLOY_EVT_NONE;
    g_rtc_wake_count = 0;
    g_ir1_count      = 0;
    g_ir2_count      = 0;
    g_ir1_last_ts    = 0;
    g_ir2_last_ts    = 0;

    interrupts();

    // Reset runtime state
    vbat_counter = 0;
    rtc_period   = config.acquisition_interval_s;

    // Register driver interrupt callbacks
    ir_driver.set_callback_sensor_1(callback_ir1);
    ir_driver.set_callback_sensor_2(callback_ir2);
    rtc_set_alarm_callback(callback_rtc);
    rtc_clear_and_set_alarm(rtc().now(), rtc_period);
}

/**
 * @brief Cleanup DEPLOY mode runtime and callbacks.
 *
 * Must be called when leaving DEPLOY state.
 *
 * @param ir_driver Reference to the IR PWM driver.
 */
void deploy_exit(ir_pwm& ir_driver) {
    noInterrupts();

    // Reset event flags and counters (ISR-related)
    g_deploy_events  = DEPLOY_EVT_NONE;
    g_rtc_wake_count = 0;
    g_ir1_count      = 0;
    g_ir2_count      = 0;
    g_ir1_last_ts    = 0;
    g_ir2_last_ts    = 0;

    interrupts();

    ir_driver.set_callback_sensor_1(nullptr);
    ir_driver.set_callback_sensor_2(nullptr);
    rtc_clear_alarm_flag();
    rtc_set_alarm_callback(nullptr);

    // Reset runtime state
    vbat_counter = 0;
    rtc_period   = 0;
}

/**
 * @brief Main DEPLOY state handler.
 *
 * This function implements one full iteration of the DEPLOY state.
 *
 * @param state Reference to the current system state. May be set to
 * @ref STATE_ENDOFLIFE by the battery check or other subsystems.
 * @param ir_driver Reference to the IR PWM driver.
 */
void run_deploy_state(SystemState& state, ir_pwm& ir_driver) {
    uint8_t events          = DEPLOY_EVT_NONE;
    uint32_t rtc_wake_count = 0;
    uint32_t ir1_count      = 0;
    uint32_t ir2_count      = 0;
    int32_t vbat_mv         = 0;
    bool changed            = false;
    DateTime now;

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

    // Common post-wake handling
    now = rtc().now();
    if (!check_and_create_new_daily_file(now)) {
        state = STATE_ENDOFLIFE;
        deploy_exit(ir_driver);
        return;
    }

    // ===== Periodic full acquisition (RTC driven) =====
    if (events & DEPLOY_EVT_RTC_WAKE) {
        // Clear alarm and program next wake-up.
        rtc_clear_and_set_alarm(now, rtc_period);

        // Read all active sensors.
        SensorFrame frame = readAllSensors(g_sensors, G_SENSOR_COUNT);

        // Log all sensor readings (AS7341, TSL2591, VBAT, etc.).
        logSensorFrame(now, frame);

        if (config.enable_vbat && batt_available) {
            vbat_counter += rtc_wake_count;

            // Perform battery check every 10 RTC wakes.
            if (vbat_counter >= VBAT_CHECK_INTERVAL) {
                vbat_counter = 0;
                battery_service_read_vbat_filtered_mv(vbat_mv, changed);
                logMeasurement(now, "VBAT", (float)vbat_mv, "mV", config.use_buffer);
                if (!battery_service_decision("Deployment", vbat_mv)) {
                    // Handle decision failure
                    error_signal(ERR_BATTERY_CRITICAL);
                    state = STATE_ENDOFLIFE;
                    deploy_exit(ir_driver);
                    return;
                }
            }
        }
    }

    // ===== Event-driven partial acquisition =====

    if (events & DEPLOY_EVT_SENSOR_AS7341) {
        // TODO: implement partial AS7341 acquisition on interrupt wake-up.
    }

    if (events & DEPLOY_EVT_SENSOR_TSL2591) {
        // TODO: implement partial TSL2591 acquisition on interrupt wake-up.
    }

    if (events & DEPLOY_EVT_SENSOR_IR1) {
        // Handle IR1 event(s) using ir1_count
        LOG_INFO("IR1 event detected (count: %d)", ir1_count);
        logMeasurement(now, "IR1_EVENT", (float)ir1_count, "count", config.use_buffer);
    }

    if (events & DEPLOY_EVT_SENSOR_IR2) {
        // Handle IR2 event(s) using ir2_count
        LOG_INFO("IR2 event detected (count: %d)", ir2_count);
        logMeasurement(now, "IR2_EVENT", (float)ir2_count, "count", config.use_buffer);
    }
}
