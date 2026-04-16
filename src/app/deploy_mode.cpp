/**
 * @file deploy_mode.cpp
 * @brief Implementation of the DEPLOY runtime state for the logger.
 *
 * This module implements the main low-power acquisition loop used during
 * deployment. In DEPLOY mode, the logger:
 *
 * - Sleeps in standby until an enabled wake source triggers.
 * - Uses RTC as the only wake source outside the active schedule window.
 * - Re-enables IR wakeups only during the active schedule window.
 * - Ensures that the daily data file exists.
 * - Reads all active sensors into a unified SensorFrame.
 * - Logs the SensorFrame to the SD card.
 * - Optionally activates RFID for a short time after IR events.
 */

//TODO: add a watchdog timer to ensure we never get stuck in this loop due to a bug or unexpected condition.
// adafruit/Adafruit SleepyDog Library@^1.8.4

#include "deploy_mode.h"
#include "sensors.h"
#include "sensors_internal.h"
#include "sd_manager.h"
#include "rtc.h"
#include "battery_service.h"
#include "config.h"
#include "log.h"
#include "error_handler.h"
#include "rfid_driver.h"
#include <ArduinoLowPower.h>
#include "utils.h"
#include "schedule_manager.h"

// Optional power control hooks.
// Replace with your actual module names if needed.
#include "pwr_manager.h"

// Event flags for DEPLOY state
enum deploy_event : uint8_t {
    DEPLOY_EVT_NONE           = 0,
    DEPLOY_EVT_RTC_WAKE       = 1 << 0,
    DEPLOY_EVT_SENSOR_AS7341  = 1 << 1,
    DEPLOY_EVT_SENSOR_TSL2591 = 1 << 2,
    DEPLOY_EVT_SENSOR_IR1     = 1 << 3,
    DEPLOY_EVT_SENSOR_IR2     = 1 << 4,
    DEPLOY_EVT_SENSOR_RFID    = 1 << 5,
};

// RFID runtime mode
enum rfid_runtime_mode : uint8_t {
    RFID_RT_DISABLED = 0,
    RFID_RT_CONTINUOUS,
    RFID_RT_ON_IR_EVENT,
};

static bool in_active_window = false;

// Global event flags set by ISRs and checked in the main loop.
static volatile uint8_t g_deploy_events = DEPLOY_EVT_NONE;

// IR event counters
static volatile uint32_t g_ir1_count = 0;
static volatile uint32_t g_ir2_count = 0;

// Last trigger timestamps (in us)
static volatile uint32_t g_ir1_last_ts = 0;
static volatile uint32_t g_ir2_last_ts = 0;

// Minimum delay between two valid events (us)
constexpr uint32_t IR_DEBOUNCE_US = 200000;

// Counter for periodic battery checks
constexpr uint8_t VBAT_CHECK_INTERVAL = 10;

// Runtime state
static uint8_t vbat_counter = 0;
static uint32_t rtc_period  = 0;

// RFID runtime state
static tag_info_t g_last_rfid_tag  = {{0}, 0};
static bool g_has_last_rfid_tag    = false;
static bool g_rfid_tag_detected    = false;
static uint32_t g_rfid_start_ms    = 0;
static uint32_t g_rfid_deadline_ms = 0;

static rfid_runtime_mode g_rfid_mode = RFID_RT_DISABLED;

// RFID timing
constexpr uint32_t RFID_CONTINUOUS_WINDOW_MS = 10;
constexpr uint32_t RFID_DEBOUNCE_MS          = 1000;
constexpr uint32_t RFID_MIN_ACTIVE_MS        = 2000;
constexpr uint32_t RFID_MAX_ACTIVE_MS        = 5000;

// Track whether external sensor wakeups are currently enabled.
static bool g_sensor_wakeups_enabled = false;

// Example schedule: active from 08:00 to 18:30 daily.
static ScheduleManager g_schedule({
    8, 0,   // start 08:00
    18, 30  // end 18:30
});
// ===== Interrupt Service Routines =====

static void callback_rtc() {
    g_deploy_events |= DEPLOY_EVT_RTC_WAKE;
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
    // Reserved for future use
    g_deploy_events |= DEPLOY_EVT_SENSOR_AS7341;
}

static void callback_tsl2591() {
    // Reserved for future use
    g_deploy_events |= DEPLOY_EVT_SENSOR_TSL2591;
}

static void callback_rfid() {
    // Reserved for future use
    g_deploy_events |= DEPLOY_EVT_SENSOR_RFID;
}

static void set_next_deploy_alarm(const DateTime& now, bool active_window) {
    if (active_window) {
        rtc_set_alarm_at(now + TimeSpan(rtc_period));
        LOG_DEBUG("Scheduled next wake-up in %lu seconds", (unsigned long)rtc_period);
    } else {
        DateTime next = g_schedule.nextStart(now);
        rtc_set_alarm_at(next);
        LOG_DEBUG("Scheduled next wake-up at %04d-%02d-%02d %02d:%02d:00", next.year(),
                  next.month(), next.day(), next.hour(), next.minute());
    }
}

/**
 * @brief Apply power and wakeup policy according to the schedule window.
 *
 * Active window:
 * - 3V rail ON
 * - IR wakeups enabled
 * - IR PWM allowed to run
 * - 5V rail ON if RFID is continuous, logic depends on RFID mode if not
 */
static void deploy_enter_active_window(ir_pwm& ir_driver, rfid_runtime_mode rfid_rt_mode) {
    pwr_manager::ir_power_on();
    ir_driver.start_pwm();

    if (!g_sensor_wakeups_enabled) {
        ir_driver.enable_sensor_wakeups();
        g_sensor_wakeups_enabled = true;
    }

    if (rfid_rt_mode == RFID_RT_CONTINUOUS) {
        pwr_manager::rfid_pwr_on(rfid_rt_mode);
    }
}

/**
 * @brief Apply power and wakeup policy for the inactive schedule window.
 * Inactive window:
 * - 3V rail OFF
 * - IR wakeups disabled
 * - IR PWM stopped
 * - 5V rail OFF
 */
static void deploy_leave_active_window(ir_pwm& ir_driver) {
    pwr_manager::rfid_pwr_off(RFID_RT_DISABLED);  // Ensure RFID is off regardless of mode.

    ir_driver.stop_pwm();

    if (g_sensor_wakeups_enabled) {
        ir_driver.disable_sensor_wakeups();
        g_sensor_wakeups_enabled = false;
    }

    pwr_manager::ir_power_off();
}

static void deploy_apply_schedule_policy(ir_pwm& ir_driver, const DateTime& now,
                                         bool& in_active_window,
                                         rfid_runtime_mode rfid_rt_mode = g_rfid_mode) {
    const bool new_active_window = g_schedule.isActive(now);

    if (new_active_window && !in_active_window) {
        deploy_enter_active_window(ir_driver, rfid_rt_mode);
    } else if (!new_active_window && in_active_window) {
        deploy_leave_active_window(ir_driver);
    }

    in_active_window = new_active_window;
}

/**
 * @brief Initialize DEPLOY mode runtime and callbacks.
 */
void deploy_enter(ir_pwm& ir_driver) {
    LOG_DEBUG("Entering DEPLOY mode: setting up callbacks and initial state");

    g_schedule.setWindow({8, 0, 18, 0});  // temporary, should come from config

    // {config.deploy_start_hour, config.deploy_start_minute, config.deploy_end_hour,
    //  config.deploy_end_minute}

    log_flush();
    noInterrupts();

    g_deploy_events = DEPLOY_EVT_NONE;
    g_ir1_count     = 0;
    g_ir2_count     = 0;
    g_ir1_last_ts   = 0;
    g_ir2_last_ts   = 0;

    interrupts();

    vbat_counter             = 0;
    rtc_period               = config.acquisition_interval_s;
    g_last_rfid_tag          = {{0}, 0};
    g_has_last_rfid_tag      = false;
    g_rfid_tag_detected      = false;
    g_rfid_start_ms          = 0;
    g_rfid_deadline_ms       = 0;
    g_sensor_wakeups_enabled = false;
    g_rfid_mode              = (rfid_runtime_mode)config.rfid_mode;
    in_active_window         = false;

    ir_driver.set_callback_sensor_1(callback_ir1);
    ir_driver.set_callback_sensor_2(callback_ir2);

    rtc_set_alarm_callback(callback_rtc);
    DateTime now = rtc().now();
    deploy_apply_schedule_policy(ir_driver, now, in_active_window, g_rfid_mode);

    set_next_deploy_alarm(now, in_active_window);
}

/**
 * @brief Cleanup DEPLOY mode runtime and callbacks.
 */
void deploy_exit(ir_pwm& ir_driver) {
    LOG_DEBUG("Exiting DEPLOY mode: clearing callbacks and state");
    log_flush();
    noInterrupts();

    g_deploy_events = DEPLOY_EVT_NONE;
    g_ir1_count     = 0;
    g_ir2_count     = 0;
    g_ir1_last_ts   = 0;
    g_ir2_last_ts   = 0;

    interrupts();

    deploy_leave_active_window(ir_driver);
    ir_driver.set_callback_sensor_1(nullptr);
    ir_driver.set_callback_sensor_2(nullptr);

    rtc_clear_alarm_flag();
    rtc_set_alarm_callback(nullptr);

    vbat_counter             = 0;
    rtc_period               = 0;
    g_last_rfid_tag          = {{0}, 0};
    g_has_last_rfid_tag      = false;
    g_rfid_tag_detected      = false;
    g_rfid_start_ms          = 0;
    g_rfid_deadline_ms       = 0;
    in_active_window         = false;
    g_sensor_wakeups_enabled = false;
    g_rfid_mode              = RFID_RT_DISABLED;
}

/**
 * @brief Main DEPLOY state handler.
 */
void run_deploy_state(SystemState& state, rfid_driver_t& rfid_driver, ir_pwm& ir_driver) {
    uint8_t events     = DEPLOY_EVT_NONE;
    uint32_t ir1_count = 0;
    uint32_t ir2_count = 0;
    int32_t vbat_mv    = 0;
    bool changed       = false;
    DateTime now;

    bool rfid_trigger = false;

    // Evaluate policy before sleeping.
    now = rtc().now();
    deploy_apply_schedule_policy(ir_driver, now, in_active_window);

    // Sleep policy:
    // - Outside active window: RTC must be the only wake source.
    // - Inside active window: keep current behavior.
    if (!in_active_window) {
        LowPower.sleep();
    } else if (g_rfid_mode == RFID_RT_CONTINUOUS) {
        LowPower.idle();
    } else {
        LowPower.sleep();
    }

    log_flush();
    noInterrupts();

    events = g_deploy_events;
    g_deploy_events &= ~events;

    ir1_count   = g_ir1_count;
    g_ir1_count = 0;

    ir2_count   = g_ir2_count;
    g_ir2_count = 0;

    interrupts();

    // Outside active window, ignore non-RTC events defensively.
    now = rtc().now();
    deploy_apply_schedule_policy(ir_driver, now, in_active_window, g_rfid_mode);

    // If we woke up outside the active window due to a non-RTC event, ignore it and go back to sleep.
    if (!in_active_window) {
        if (!(events & DEPLOY_EVT_RTC_WAKE)) {
            return;
        }
    }

    // If no event occurred and RFID is not continuous, exit early.
    if (events == DEPLOY_EVT_NONE && g_rfid_mode != RFID_RT_CONTINUOUS) {
        return;
    }

    // Common post-wake handling
    if ((events != DEPLOY_EVT_NONE) || (g_rfid_mode == RFID_RT_CONTINUOUS)) {
        if (!check_and_create_new_daily_file(now)) {
            state = STATE_ENDOFLIFE;
            return;
        }
    }

    // ===== Periodic full acquisition (RTC driven) =====

    // If we woke up due to RTC, perform a full periodic acquisition and log.
    if (events & DEPLOY_EVT_RTC_WAKE) {

        set_next_deploy_alarm(now, in_active_window);
        // LOG_DEBUG("RTC wake-up event. Performing periodic acquisition.");

        // Reserved for future use: read all sensors and log a full SensorFrame.
        // SensorFrame frame = readAllSensors(g_sensors, G_SENSOR_COUNT);
        // logSensorFrame(now, frame);

        if (config.enable_vbat && battery_is_available()) {
            vbat_counter++;

            if (vbat_counter >= VBAT_CHECK_INTERVAL) {
                vbat_counter = 0;

                if (!battery_service_read_vbat_filtered_mv(vbat_mv, changed)) {
                    LOG_ERROR("Failed to read filtered VBAT value");
                    state = STATE_ENDOFLIFE;
                    return;
                } else if (changed) {
                    LOG_INFO("VBAT changed: %d mV", vbat_mv);
                    logMeasurement(now, "VBAT", (float)vbat_mv, "mV", config.use_buffer);
                }

                if (!battery_service_decision("Deployment", vbat_mv)) {
                    error_signal(ERR_BATTERY_CRITICAL);
                    state = STATE_ENDOFLIFE;
                    return;
                }
            }
        }
    }

    // ===== Event-driven partial acquisition =====

    if (events & DEPLOY_EVT_SENSOR_AS7341) {
        // Reserved for future use.
    }

    if (events & DEPLOY_EVT_SENSOR_TSL2591) {
        // Reserved for future use.
    }

    if (events & DEPLOY_EVT_SENSOR_IR1) {
        LOG_INFO("IR1 event detected (count: %d)", ir1_count);
        logMeasurement(now, "IR1_EVENT", (float)ir1_count, "count", config.use_buffer);
    }

    if (events & DEPLOY_EVT_SENSOR_IR2) {
        LOG_INFO("IR2 event detected (count: %d)", ir2_count);
        logMeasurement(now, "IR2_EVENT", (float)ir2_count, "count", config.use_buffer);
    }

    // ===== RFID trigger handling =====

    const bool rfid_continuous  = (g_rfid_mode == RFID_RT_CONTINUOUS);
    const bool rfid_on_ir_event = (g_rfid_mode == RFID_RT_ON_IR_EVENT);
    const bool ir_event = (events & DEPLOY_EVT_SENSOR_IR1) || (events & DEPLOY_EVT_SENSOR_IR2);

    rfid_trigger = rfid_continuous || (rfid_on_ir_event && ir_event);

    // Switch on RFID if needed.
    if (rfid_trigger && !pwr_manager::rfid_is_on()) {
        pwr_manager::rfid_pwr_on(g_rfid_mode);
        g_rfid_tag_detected = false;
        g_rfid_start_ms     = millis();
        g_rfid_deadline_ms  = g_rfid_start_ms + RFID_MAX_ACTIVE_MS;

        rfid_driver::poll_now(&rfid_driver);
    }

    if (pwr_manager::rfid_is_on()) {
        // Poll RFID driver and process and queue tags.
        rfid_driver::tick(&rfid_driver);

        tag_info_t tag;
        // Process all available tags in the FIFO.
        while (rfid_driver::get_tag(&rfid_driver, &tag)) {
            LOG_DEBUG("Received RFID tag: %s (time since poll: %d ms)", tag.tag,
                      millis() - tag.time_ms);
            bool should_log =
                !g_has_last_rfid_tag ||
                rfid_driver::should_record_tag(&g_last_rfid_tag, &tag, RFID_DEBOUNCE_MS);

            if (should_log) {
                LOG_INFO("RFID tag detected: %s", tag.tag);
                DateTime tag_now = rtc().now();
                logMeasurement(tag_now, "RFID_TAG", 1.0f, tag.tag, config.use_buffer);

                g_last_rfid_tag     = tag;
                g_has_last_rfid_tag = true;
                g_rfid_tag_detected = true;
            }
        }

        if (!rfid_continuous) {
            const uint32_t now_ms  = millis();
            const uint32_t elapsed = now_ms - g_rfid_start_ms;

            if (elapsed >= RFID_MIN_ACTIVE_MS &&
                (g_rfid_tag_detected || now_ms >= g_rfid_deadline_ms)) {
                pwr_manager::rfid_pwr_off(g_rfid_mode);

                g_rfid_tag_detected = false;
                g_rfid_start_ms     = 0;
                g_rfid_deadline_ms  = 0;
            }
        }
    }
}
