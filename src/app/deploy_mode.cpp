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
#include <ArduinoLowPower.h>
#include "utils.h"
#include "assembly.h"
#include "hardware.h"

/// Counter used to decimate battery checks during deployment.
static uint8_t vbatCounter = 0;

volatile bool g_rtcWake     = false;
volatile bool g_sensorReady = false;

void rtcIsr() {
    g_rtcWake = true;
}

void sensorIsr() {
    g_sensorReady = true;
}

// Event flags for DEPLOY state
enum DeployEvent : uint8_t {
    DEPLOY_EVT_NONE           = 0,
    DEPLOY_EVT_RTC_WAKE       = 1 << 0,
    DEPLOY_EVT_SENSOR_AS7341  = 1 << 1,
    DEPLOY_EVT_SENSOR_TSL2591 = 1 << 2
};

volatile uint8_t g_deploy_events = DEPLOY_EVT_NONE;
volatile uint32_t g_rtcWakeCount = 0;

// ===== Interrupt Service Routines =====

void rtcWakeISR() {
    g_deploy_events |= DEPLOY_EVT_RTC_WAKE;
    g_rtcWakeCount++;
}

void as7341ISR() {
    g_deploy_events |= DEPLOY_EVT_SENSOR_AS7341;
}

void tsl2591ISR() {
    g_deploy_events |= DEPLOY_EVT_SENSOR_TSL2591;
}

/**
 * @brief Main DEPLOY state handler.
 *
 * This function implements one full iteration of the DEPLOY state.
 *
 * @param state Reference to the current system state. May be set to
 *              @ref STATE_ENDOFLIFE by the battery check or other subsystems.
 */
void runDeployState(SystemState& state) {

    uint8_t events        = DEPLOY_EVT_NONE;
    uint32_t rtcWakeCount = 0;

    int32_t vbat_mv = 0;
    bool changed    = false;

    if (hw_assembly.rtc_type == "ds3231") {
        // Enter low-power sleep; RTC alarm or sensor interrupt will wake the MCU.
        LowPower.sleep();

        noInterrupts();
        uint8_t events = g_deploy_events;
        g_deploy_events &= ~events;  // clear only handled events
        rtcWakeCount   = g_rtcWakeCount;
        g_rtcWakeCount = 0;
        interrupts();

        // If wake-up was not caused by the RTC alarm or a sensor interrupt, exit early.
        if (events == DEPLOY_EVT_NONE) return;

        if (events & DEPLOY_EVT_RTC_WAKE) {
            // Clear alarm and program next wake-up.
            rtc_clear_alarm_flag();
            rtc_schedule_next_wake(rtc.now(), config.acquisition_interval_s, RTC_INTERRUPT_PIN,
                                   rtcWakeISR);
        }

        if (events & DEPLOY_EVT_SENSOR_AS7341) {
            // AS7341 interrupt detected; handle sensor-specific work outside ISR.
            // Example: read spectral data ready flag or FIFO.
        }

        if (events & DEPLOY_EVT_SENSOR_TSL2591) {
            // TSL2591 interrupt detected; handle light threshold or data ready.
        }
    }

    // ===== Periodic full acquisition (RTC driven) =====
    if (events & DEPLOY_EVT_RTC_WAKE) {

        // Ensure today’s data file exists.
        DateTime now = rtc.now();
        check_and_create_new_daily_file(now);

        // Read all active sensors.
        SensorFrame frame = readAllSensors(g_sensors, G_SENSOR_COUNT);

        // Log all sensor readings (AS7341, TSL2591, VBAT, etc.).
        logSensorFrame(now, frame);

        // Perform periodic battery check every 10 RTC wakes.
        if (rtcWakeCount >= 10) {
            rtcWakeCount = 0;
            battery_service_read_vbat_filtered_mv(vbat_mv, changed);
            if (!battery_service_decision("Boot", vbat_mv)) {
                // Handle decision failure
                state = STATE_ENDOFLIFE;
                return;
            }
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
}
