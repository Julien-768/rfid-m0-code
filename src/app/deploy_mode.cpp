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
static bool g_rfid_active          = false;
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
    g_deploy_events |= DEPLOY_EVT_SENSOR_AS7341;
}

static void callback_tsl2591() {
    g_deploy_events |= DEPLOY_EVT_SENSOR_TSL2591;
}

static void callback_rfid() {
    g_deploy_events |= DEPLOY_EVT_SENSOR_RFID;
}

static void rfid_driver_force_poll(rfid_driver_t* drv) {
    if (!drv || !drv->port) return;

    switch (drv->type) {
        case TAG_TYPE_FDX:
            drv->port->print("@rq\r");
            break;
        case TAG_TYPE_HDX:
            drv->port->print("@todo\r");
            break;
        case TAG_TYPE_EM4102:
            drv->port->print("@ru\r");
            break;
        default:
            break;
    }
}

/**
 * @brief Return true if the current time is inside the active deploy window.
 *
 * Replace this implementation with your real schedule helper if you already
 * have one elsewhere in the project.
 */
static bool deploy_is_in_active_window(const DateTime& now) {
    // Example: always active.
    // TODO: replace with real schedule logic.
    (void)now;
    return true;
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
        g_rfid_active = pwr_manager::rfid_pwr_on(rfid_rt_mode);
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
    pwr_manager::rfid_pwr_off(RFID_RT_DISABLED);
    g_rfid_active = false;

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
    const bool new_active_window = deploy_is_in_active_window(now);

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
void deploy_enter(ir_pwm& ir_driver, rfid_driver_t& rfid_driver) {
    LOG_DEBUG("Entering DEPLOY mode: setting up callbacks and initial state");

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
    g_rfid_active            = false;
    g_rfid_tag_detected      = false;
    g_rfid_start_ms          = 0;
    g_rfid_deadline_ms       = 0;
    g_sensor_wakeups_enabled = false;

    ir_driver.set_callback_sensor_1(callback_ir1);
    ir_driver.set_callback_sensor_2(callback_ir2);

    rtc_set_alarm_callback(callback_rtc);
    rtc_clear_and_set_alarm(rtc().now(), rtc_period);

    g_rfid_mode = config.rfid_mode;

    // Apply initial schedule policy immediately.
    deploy_apply_schedule_policy(ir_driver, rtc().now(), in_active_window, g_rfid_mode);
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
    ir_driver.set_callback_sensor_1(nullptr);  // needed ?
    ir_driver.set_callback_sensor_2(nullptr);  // needed ?

    rtc_clear_alarm_flag();
    rtc_set_alarm_callback(nullptr);  // needed ?

    vbat_counter        = 0;
    rtc_period          = 0;
    g_last_rfid_tag     = {{0}, 0};
    g_has_last_rfid_tag = false;
    g_rfid_active       = false;
    g_rfid_tag_detected = false;
    g_rfid_start_ms     = 0;
    g_rfid_deadline_ms  = 0;
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

    events          = g_deploy_events;
    g_deploy_events = DEPLOY_EVT_NONE;

    ir1_count   = g_ir1_count;
    g_ir1_count = 0;

    ir2_count   = g_ir2_count;
    g_ir2_count = 0;

    interrupts();

    // Outside active window, ignore non-RTC events defensively.
    now = rtc().now();
    deploy_apply_schedule_policy(ir_driver, now, in_active_window, g_rfid_mode);

    if (!in_active_window) {
        if (!(events & DEPLOY_EVT_RTC_WAKE)) {
            return;
        }
    }

    // If no event occurred and RFID is not continuous, exit early.
    if (events == DEPLOY_EVT_NONE && g_rfid_mode != RFID_RT_CONTINUOUS) {
        return;
    }

    now = rtc().now();

    // Common post-wake handling
    if ((events != DEPLOY_EVT_NONE) || (g_rfid_mode == RFID_RT_CONTINUOUS)) {
        if (!check_and_create_new_daily_file(now)) {
            state = STATE_ENDOFLIFE;
            return;
        }
    }

    // ===== Periodic full acquisition (RTC driven) =====
    if (events & DEPLOY_EVT_RTC_WAKE) {
        rtc_clear_and_set_alarm(now, rtc_period);

        LOG_DEBUG("RTC wake-up event. Scheduled next wake-up in %d seconds", rtc_period);

        SensorFrame frame = readAllSensors(g_sensors, G_SENSOR_COUNT);
        logSensorFrame(now, frame);

        if (config.enable_vbat && battery_is_available()) {
            vbat_counter++;

            if (vbat_counter >= VBAT_CHECK_INTERVAL) {
                vbat_counter = 0;

                if (!battery_service_read_vbat_filtered_mv(vbat_mv, changed)) {
                    LOG_ERROR("Failed to read filtered VBAT value");
                    state = STATE_ENDOFLIFE;
                    return;
                } else if (changed) {
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
        // For future use.
    }

    if (events & DEPLOY_EVT_SENSOR_TSL2591) {
        // For future use.
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

    if (g_rfid_mode == RFID_RT_ON_IR_EVENT) {
        if ((events & DEPLOY_EVT_SENSOR_IR1) || (events & DEPLOY_EVT_SENSOR_IR2)) {
            rfid_trigger = true;
        }
    }
    if (g_rfid_mode == RFID_RT_CONTINUOUS) {
        rfid_trigger = true;
    }

    // Switch on RFID not already active
    if (rfid_trigger && !g_rfid_active) {

        g_rfid_active       = pwr_manager::rfid_pwr_on(g_rfid_mode);
        g_rfid_tag_detected = false;
        g_rfid_start_ms     = millis();
        g_rfid_deadline_ms  = g_rfid_start_ms + RFID_MAX_ACTIVE_MS;

        rfid_driver_force_poll(&rfid_driver);
        // TODO: do not sleep during the active window if RFID is ON_IR_EVENT to avoid missing tags, or implement a more robust wake-up strategy for RFID events (e.g. dedicated RFID wake-up pin or interrupt from RFID driver)
    }

    if (g_rfid_active) {
        rfid_driver_tick(&rfid_driver);

        tag_info_t tag;
        while (rfid_driver_get_tag(&rfid_driver, &tag)) {
            bool should_log = true;

            if (g_has_last_rfid_tag) {
                should_log = rfid_should_record_tag(&g_last_rfid_tag, &tag, RFID_DEBOUNCE_MS);
            }

            if (should_log) {
                LOG_INFO("RFID tag detected: %s", tag.tag);
                DateTime tag_now = rtc().now();
                logMeasurement(tag_now, "RFID_TAG", 1.0f, tag.tag, config.use_buffer);

                g_last_rfid_tag     = tag;
                g_has_last_rfid_tag = true;
                g_rfid_tag_detected = true;
            }
        }

        uint32_t now_ms  = millis();
        uint32_t elapsed = now_ms - g_rfid_start_ms;

        if (elapsed >= RFID_MIN_ACTIVE_MS) {
            if (g_rfid_tag_detected || now_ms >= g_rfid_deadline_ms) {

                if (g_rfid_mode != RFID_RT_CONTINUOUS) {
                    pwr_manager::rfid_pwr_off(g_rfid_mode);
                    g_rfid_active = false;
                }

                g_rfid_tag_detected = false;
                g_rfid_start_ms     = 0;
                g_rfid_deadline_ms  = 0;
                if (g_rfid_mode != RFID_RT_CONTINUOUS) {
                    rfid_trigger = false;
                }
            }
        }
    } else if (g_rfid_active && g_rfid_mode != RFID_RT_CONTINUOUS) {
        pwr_manager::rfid_pwr_off(g_rfid_mode);
        g_rfid_active = false;
    }
}
