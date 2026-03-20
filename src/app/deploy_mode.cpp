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

/// Counter used to decimate battery checks during deployment.
static uint8_t vbatCounter = 0;

/**
 * @brief Main DEPLOY state handler.
 *
 * This function implements one full iteration of the DEPLOY state:
 *
 * 1. Enter low-power sleep using ArduinoLowPower.
 * 2. Wake up only when the RTC alarm triggers.
 * 3. Clear the alarm flag and schedule the next wake-up according to
 *    @ref config.acquisition_interval_s.
 * 4. Ensure the correct daily log file is open via @ref check_and_create_new_daily_file().
 * 5. Read all enabled sensors into a @ref SensorFrame with @ref readAllSensors().
 * 6. Log the SensorFrame to the SD card using @ref logSensorFrame().
 * 7. Every 10 acquisition cycles, check the battery voltage via
 *    @ref checkBatteryStatus(), and if it requests END-OF-LIFE, log a warning.
 *
 * @param state Reference to the current system state. May be set to
 *              @ref STATE_ENDOFLIFE by the battery check or other subsystems.
 */
void runDeployState(SystemState& state) {

    // 1) Enter low-power sleep; RTC alarm will wake the MCU.
    LowPower.sleep();

    // 2) If wake-up was not caused by the RTC alarm, exit early.
    if (!alarm_triggered()) return;

    // 3) Clear alarm and program next wake-up.
    clear_alarm_flag();
    rtc_schedule_next_wake(rtc.now(), config.acquisition_interval_s);

    // 4) Ensure today’s data file exists.
    DateTime now = rtc.now();
    check_and_create_new_daily_file(now);

    // 5) Read all active sensors.
    SensorFrame frame = readAllSensors(g_sensors, G_SENSOR_COUNT);

    // 6) Log all sensor readings (AS7341, TSL2591, VBAT, etc.).
    logSensorFrame(now, frame);

    // 7) Periodic battery monitoring (every 10 samples).
    if (frame.valid_vbat)
        {
            // TODO battery
            if (!battery_service_periodic_check(frame.vbat_mv, vbatCounter, 10))
                {
                    state = STATE_ENDOFLIFE;
                    return;
            }
    }
}
