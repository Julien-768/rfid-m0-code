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

/**
 * @section platform_info Platform Information
 * - Platform: Adafruit Feather M0 (ATSAMD21G18)
 * - MCU: ARM Cortex-M0+ @ 48 MHz
 * - Framework: Arduino (SAMD core)
 * - Logic Level: 3.3V
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
    DEPLOY_EVT_BUTTON_PRESS   = 1 << 6,
    DEPLOY_EVT_BUTTON_RELEASE = 1 << 7,
};

// RFID runtime mode
enum rfid_runtime_mode : uint8_t {
    RFID_RT_DISABLED = 0,
    RFID_RT_CONTINUOUS,
    RFID_RT_ON_IR_EVENT,
};

static bool ir_enabled = true;

static bool in_awake_window = false;

// Global event flags set by ISRs and checked in the main loop.
static volatile uint8_t g_deploy_events = DEPLOY_EVT_NONE;

// IR event counters
static volatile uint32_t g_ir1_count = 0;
static volatile uint32_t g_ir2_count = 0;

// Last trigger timestamps (ms)
static volatile uint32_t g_ir1_last_ts = 0;
static volatile uint32_t g_ir2_last_ts = 0;

// Last observed pin states for the shared IR interrupt callback.
static volatile uint8_t g_ir1_last_state = HIGH;
static volatile uint8_t g_ir2_last_state = HIGH;

// Minimum delay between two valid events (ms)
constexpr uint32_t IR_DEBOUNCE_MS = 200;

// Counter for periodic battery checks
constexpr uint8_t VBAT_CHECK_INTERVAL = 10;

// Runtime state
static uint8_t vbat_counter = 0;
static uint32_t rtc_period  = 0;

// RFID runtime state
static tag_info_t g_last_rfid_tag   = {{0}, 0};
static bool g_has_last_rfid_tag     = false;
static bool g_rfid_tag_detected     = false;
static uint32_t g_rfid_triggered_ms = 0;
static uint32_t g_rfid_deadline_ms  = 0;

static rfid_runtime_mode g_rfid_mode = RFID_RT_DISABLED;

// RFID timing
constexpr uint32_t RFID_CONTINUOUS_WINDOW_MS = 10;
constexpr uint32_t RFID_DEBOUNCE_MS          = 1000;
constexpr uint32_t RFID_MIN_ACTIVE_MS        = 2000;
constexpr uint32_t RFID_MAX_ACTIVE_MS        = 5000;

// Track whether external sensor wakeups are currently enabled.
static bool g_sensor_wakeups_enabled = false;

// --- Power button integration state ---
static bool g_pw_button_was_pressed                  = false;
static uint32_t g_pw_button_press_start_ms           = 0;
static uint32_t g_pw_button_last_log_ms              = 0;
constexpr uint32_t DEPLOY_POWER_BUTTON_LONG_PRESS_MS = 12000;  // 12 seconds

// Button timing captured in the interrupt callback.
// IMPORTANT: pwr_manager::enable_button_wakeup() must call callback_button()
// on both button edges (press and release / CHANGE), otherwise very short
// presses that are already released before the main loop runs cannot be timed.
static volatile bool g_button_irq_is_pressed       = false;
static volatile uint32_t g_button_irq_press_ms     = 0;
static volatile uint32_t g_button_irq_release_ms   = 0;
static volatile uint32_t g_button_irq_duration_ms  = 0;
static volatile bool g_button_irq_duration_pending = false;

struct button_irq_snapshot_t {
    bool is_pressed;
    uint32_t press_ms;
    uint32_t release_ms;
    uint32_t duration_ms;
    bool duration_pending;
};

static void reset_button_irq_state() {
    g_button_irq_is_pressed       = false;
    g_button_irq_press_ms         = 0;
    g_button_irq_release_ms       = 0;
    g_button_irq_duration_ms      = 0;
    g_button_irq_duration_pending = false;
}

static void reset_power_button_tracking() {
    g_pw_button_was_pressed    = false;
    g_pw_button_press_start_ms = 0;
    g_pw_button_last_log_ms    = 0;

    noInterrupts();
    g_button_irq_is_pressed       = false;
    g_button_irq_press_ms         = 0;
    g_button_irq_release_ms       = 0;
    g_button_irq_duration_ms      = 0;
    g_button_irq_duration_pending = false;
    interrupts();
}

static void log_user_battery_check() {
    int32_t vbat_mv = 0;
    bool changed    = false;

    LOG_INFO("Short power-button press: battery check requested");
    if (config.enable_vbat && battery_is_available() &&
        battery_service_read_vbat_filtered_mv(vbat_mv, changed)) {
        logMeasurement(rtc().now(), "VBAT_USER_CHECK", (float)vbat_mv, "mV", config.use_buffer);
    }
}

static bool consume_button_irq_duration(uint32_t& duration_ms) {
    if (!g_button_irq_duration_pending) {
        return false;
    }

    duration_ms = g_button_irq_duration_ms;
    reset_button_irq_state();
    return true;
}

static void deploy_handle_power_button(SystemState& state, ir_pwm& ir_driver,
                                       const button_irq_snapshot_t& button_irq) {
    (void)ir_driver;

    // Best case: the ISR saw both edges. This measures even very short presses
    // that were already released before the main loop got CPU time.
    if (button_irq.duration_pending) {
        const uint32_t press_duration = button_irq.duration_ms;

        LOG_INFO("Button released after %lu ms", press_duration);
        LOG_INFO("Button pressed for %lu ms", press_duration);
        reset_button_irq_state();

        if (press_duration >= DEPLOY_POWER_BUTTON_LONG_PRESS_MS) {
            LOG_WARN("Long power-button press: user shutdown requested");
            logMeasurement(rtc().now(), "SHUTDOWN_USER", 1.0f, "count", config.use_buffer);
            log_flush();
            state = STATE_ENDOFLIFE;
            reset_power_button_tracking();
            return;
        }

        log_user_battery_check();
        g_pw_button_was_pressed    = false;
        g_pw_button_press_start_ms = 0;
        g_pw_button_last_log_ms    = 0;
        return;
    }

    // If the button is still held, keep polling only to detect a long press
    // before release. The start timestamp still comes from the ISR.
    if (pwr_manager::power_button_read_state()) {
        g_pw_button_was_pressed    = true;
        g_pw_button_press_start_ms = (button_irq.press_ms != 0) ? button_irq.press_ms : millis();
        g_pw_button_last_log_ms    = millis();

        while (pwr_manager::power_button_read_state()) {
            const uint32_t now            = millis();
            const uint32_t press_duration = now - g_pw_button_press_start_ms;

            if (press_duration >= DEPLOY_POWER_BUTTON_LONG_PRESS_MS) {
                LOG_INFO("Button pressed for %lu ms", press_duration);
                LOG_WARN("Long power-button press: user shutdown requested");
                logMeasurement(rtc().now(), "SHUTDOWN_USER", 1.0f, "count", config.use_buffer);
                log_flush();
                state = STATE_ENDOFLIFE;
                reset_power_button_tracking();
                return;
            }
        }

        // Release happened while we were polling. If the release ISR also ran,
        // the next wake/event will contain the exact ISR duration. Otherwise,
        // this is still a correct fallback based on the ISR press timestamp.
        const uint32_t press_duration = millis() - g_pw_button_press_start_ms;

        LOG_INFO("Button released after %lu ms", press_duration);
        LOG_INFO("Button pressed for %lu ms", press_duration);

        reset_button_irq_state();

        g_pw_button_was_pressed    = false;
        g_pw_button_press_start_ms = 0;
        g_pw_button_last_log_ms    = 0;

        if (press_duration >= DEPLOY_POWER_BUTTON_LONG_PRESS_MS) {
            LOG_WARN("Long power-button press: user shutdown requested");
            logMeasurement(rtc().now(), "SHUTDOWN_USER", 1.0f, "count", config.use_buffer);
            log_flush();
            state = STATE_ENDOFLIFE;
            reset_power_button_tracking();
            return;
        }

        log_user_battery_check();
        return;
    }

    // If we get here, the main loop saw a button event but neither an ISR-complete
    // duration nor a currently pressed button. That means the wake callback is
    // probably configured on press only, not CHANGE.
    LOG_INFO("Button press was already released before it could be measured");
    reset_button_irq_state();
    log_user_battery_check();
}
// Example schedule: active from 08:00 to 18:30 daily.
static ScheduleManager g_schedule({
    8, 0,   // start 08:00
    18, 30  // end 18:30
});
// ===== Interrupt Service Routines =====

static void callback_rtc() {
    g_deploy_events |= DEPLOY_EVT_RTC_WAKE;
}

static void register_ir1_event(uint32_t now) {
    if ((now - g_ir1_last_ts) < IR_DEBOUNCE_MS) {
        return;
    }

    g_ir1_last_ts = now;
    g_ir1_count++;
    g_deploy_events |= DEPLOY_EVT_SENSOR_IR1;
    LOG_DEBUG("IR1 interrupt triggered");
}

static void register_ir2_event(uint32_t now) {
    if ((now - g_ir2_last_ts) < IR_DEBOUNCE_MS) {
        return;
    }

    g_ir2_last_ts = now;
    g_ir2_count++;
    g_deploy_events |= DEPLOY_EVT_SENSOR_IR2;
    LOG_DEBUG("IR2 interrupt triggered");
}

/**
 * @brief Shared IR callback for pins that use the same SAMD21 EXTINT line.
 *
 * Pins 10 and 14 both map to EXTINT[2] on the Feather M0. Therefore the ISR
 * wrapper that fires is not reliable enough to identify the physical sensor.
 * This callback is installed for both IR callbacks and determines the source by
 * reading both pins and comparing them with their last known states.
 */
static void callback_ir_shared(uint8_t /*unused_state*/) {
    const uint32_t now = millis();

    const uint8_t ir1_state = digitalRead(PIN_PR_1);
    const uint8_t ir2_state = digitalRead(PIN_PR_2);

    if (ir1_state != g_ir1_last_state) {
        g_ir1_last_state = ir1_state;
        register_ir1_event(now);
    }

    if (ir2_state != g_ir2_last_state) {
        g_ir2_last_state = ir2_state;
        register_ir2_event(now);
    }
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

static void callback_button() {
    const uint32_t now    = millis();
    const bool is_pressed = pwr_manager::power_button_read_state();

    if (is_pressed) {
        if (!g_button_irq_is_pressed) {
            g_button_irq_is_pressed = true;
            g_button_irq_press_ms   = now;
            g_deploy_events |= DEPLOY_EVT_BUTTON_PRESS;
        }
    } else {
        if (g_button_irq_is_pressed) {
            g_button_irq_is_pressed       = false;
            g_button_irq_release_ms       = now;
            g_button_irq_duration_ms      = now - g_button_irq_press_ms;
            g_button_irq_duration_pending = true;
            g_deploy_events |= DEPLOY_EVT_BUTTON_RELEASE;
        }
    }
}

static void set_next_deploy_alarm(const DateTime& now, bool active_window) {
    DateTime next;
    if (active_window) {
        next = now + TimeSpan(rtc_period);
    } else {
        next = g_schedule.nextStart(now);
    }
    rtc_set_alarm_at(next);
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
    if (ir_enabled) {
        pwr_manager::ir_power_on();
        ir_driver.start_pwm();
        if (!g_sensor_wakeups_enabled) {
            ir_driver.enable_sensor_wakeups();
            g_sensor_wakeups_enabled = true;
        }
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

    if (ir_enabled) {

        // ir_driver.stop_pwm();
        // if (g_sensor_wakeups_enabled) {
        //     ir_driver.disable_sensor_wakeups();
        //     g_sensor_wakeups_enabled = false;
        // }
        // pwr_manager::ir_power_off();
    }
}

static void apply_awake_window(ir_pwm& ir_driver, const DateTime& now, bool& in_awake_window,
                               rfid_runtime_mode rfid_rt_mode = g_rfid_mode) {
    const bool new_active_window = g_schedule.isActive(now);

    if (new_active_window && !in_awake_window) {
        deploy_enter_active_window(ir_driver, rfid_rt_mode);
    } else if (!new_active_window && in_awake_window) {
        deploy_leave_active_window(ir_driver);
    }

    in_awake_window = new_active_window;
}

/**
 * @brief Initialize DEPLOY mode runtime and callbacks.
 */
void deploy_enter(ir_pwm& ir_driver) {
    LOG_DEBUG("Initializing DEPLOY mode: setting up callbacks and initial state");

    g_schedule.setWindow({8, 0, 18, 0});  // temporary, should come from config
    rtc_period      = config.acquisition_interval_s;
    vbat_counter    = 0;
    g_rfid_mode     = (rfid_runtime_mode)config.rfid_mode;
    in_awake_window = false;

    g_last_rfid_tag          = {{0}, 0};
    g_has_last_rfid_tag      = false;
    g_rfid_tag_detected      = false;
    g_rfid_triggered_ms      = 0;
    g_rfid_deadline_ms       = 0;
    g_sensor_wakeups_enabled = false;

    // {config.deploy_start_hour, config.deploy_start_minute, config.deploy_end_hour,
    //  config.deploy_end_minute}

    log_flush();

    LOG_DEBUG("Checking initial schedule window at startup");
    rtc_clear_alarm_flag();
    delay(100);  // Ensure RTC alarm flag is cleared before setting
    DateTime now = rtc().now();
    apply_awake_window(ir_driver, now, in_awake_window, g_rfid_mode);

    noInterrupts();

    g_deploy_events               = DEPLOY_EVT_NONE;
    g_ir1_count                   = 0;
    g_ir2_count                   = 0;
    g_ir1_last_ts                 = 0;
    g_ir2_last_ts                 = 0;
    g_ir1_last_state              = digitalRead(PIN_PR_1);
    g_ir2_last_state              = digitalRead(PIN_PR_2);
    g_button_irq_is_pressed       = false;
    g_button_irq_press_ms         = 0;
    g_button_irq_release_ms       = 0;
    g_button_irq_duration_ms      = 0;
    g_button_irq_duration_pending = false;

    interrupts();

    LOG_DEBUG("Setting up DEPLOY mode callbacks");
    pwr_manager::enable_button_wakeup(callback_button);
    rtc_set_alarm_callback(callback_rtc);
    set_next_deploy_alarm(now, in_awake_window);

    ir_driver.set_callback_sensor_1(callback_ir_shared);
    ir_driver.set_callback_sensor_2(callback_ir_shared);

    g_pw_button_was_pressed    = pwr_manager::power_button_read_state();
    g_pw_button_press_start_ms = g_pw_button_was_pressed ? millis() : 0;
    g_pw_button_last_log_ms    = 0;
}

/**
 * @brief Cleanup DEPLOY mode runtime and callbacks.
 */
void deploy_exit(ir_pwm& ir_driver) {
    LOG_DEBUG("Exiting DEPLOY mode: clearing callbacks and state");
    log_flush();
    noInterrupts();

    g_deploy_events               = DEPLOY_EVT_NONE;
    g_ir1_count                   = 0;
    g_ir2_count                   = 0;
    g_ir1_last_ts                 = 0;
    g_ir2_last_ts                 = 0;
    g_ir1_last_state              = HIGH;
    g_ir2_last_state              = HIGH;
    g_button_irq_is_pressed       = false;
    g_button_irq_press_ms         = 0;
    g_button_irq_release_ms       = 0;
    g_button_irq_duration_ms      = 0;
    g_button_irq_duration_pending = false;

    interrupts();

    pwr_manager::disable_button_wakeup();

    deploy_leave_active_window(ir_driver);
    ir_driver.set_callback_sensor_1(nullptr);
    ir_driver.set_callback_sensor_2(nullptr);

    rtc_clear_alarm_flag();
    rtc_set_alarm_callback(nullptr);

    vbat_counter               = 0;
    rtc_period                 = 0;
    g_last_rfid_tag            = {{0}, 0};
    g_has_last_rfid_tag        = false;
    g_rfid_tag_detected        = false;
    g_rfid_triggered_ms        = 0;
    g_rfid_deadline_ms         = 0;
    in_awake_window            = false;
    g_sensor_wakeups_enabled   = false;
    g_rfid_mode                = RFID_RT_DISABLED;
    g_pw_button_was_pressed    = false;
    g_pw_button_press_start_ms = 0;
    g_pw_button_last_log_ms    = 0;
}

int count_4_dot          = 0;
uint32_t rfid_start_time = 0;
uint32_t now_ms          = 0;
uint32_t elapsed         = 0;

/**
 * @brief Main DEPLOY state handler.
 */
void run_deploy_state(SystemState& state, rfid_driver_t& rfid_driver, ir_pwm& ir_driver) {
    uint8_t events                   = DEPLOY_EVT_NONE;
    button_irq_snapshot_t button_irq = {};
    uint32_t ir1_count               = 0;
    uint32_t ir2_count               = 0;
    int32_t vbat_mv                  = 0;
    bool changed                     = false;
    DateTime now;

    bool rfid_trigger = false;

    // Evaluate policy before sleeping.
    // now = rtc().now();
    // apply_awake_window(ir_driver, now, in_awake_window);

    // Sleep policy:
    // - Outside active window: RTC must be the only wake source.
    // - Inside active window: keep current behavior.
    if (!in_awake_window) {
        LOG_DEBUG("Entering low-power mode. In active window: NO");
        LowPower.sleep();
    } else {
        // LOG_DEBUG("Before sleep: rtc_irq=%d (0 means active irq, 1 means no irq), events=0x%02X",
        //           digitalRead(10), g_deploy_events);
        // LowPower.sleep();

        LowPower.idle();
        delay(200);

        // LOG_DEBUG("After wake: rtc_irq=%d (0 means active irq, 1 means no irq), events=0x%02X",
        //           digitalRead(10), g_deploy_events);
    }

    noInterrupts();

    events = g_deploy_events;
    g_deploy_events &= ~events;

    button_irq.is_pressed       = g_button_irq_is_pressed;
    button_irq.press_ms         = g_button_irq_press_ms;
    button_irq.release_ms       = g_button_irq_release_ms;
    button_irq.duration_ms      = g_button_irq_duration_ms;
    button_irq.duration_pending = g_button_irq_duration_pending;

    ir1_count   = g_ir1_count;
    g_ir1_count = 0;

    ir2_count   = g_ir2_count;
    g_ir2_count = 0;

    interrupts();

    // Outside active window, ignore non-RTC events defensively.
    now = rtc().now();
    // apply_awake_window(ir_driver, now, in_awake_window, g_rfid_mode);

    // If we woke up outside the active window due to a non-RTC event, ignore it and go back to sleep.
    if (!in_awake_window) {
        if (!(events & DEPLOY_EVT_RTC_WAKE)) {
            LOG_DEBUG("Woke up outside active window, events=0x%02X. Ignoring non-RTC events.",
                      events);
            return;
        }
    }

    // If no event occurred and RFID is not continuous, exit early.
    if (events == DEPLOY_EVT_NONE && pwr_manager::rfid_is_on() == false) {
        // LOG_DEBUG("Woke up from idle. No events to process.");
        // temp

        // /end temp
        if (count_4_dot < 4) {
#if (LOG_SERIAL_OUTPUT == LOG_SERIAL1)
            Serial1.print(".");
#endif
            count_4_dot++;
        }
        if (count_4_dot >= 4) {
#if (LOG_SERIAL_OUTPUT == LOG_SERIAL1)
            Serial1.println(".");
#endif
            count_4_dot = 0;
        }
    }

    // Common post-wake handling
    if ((events != DEPLOY_EVT_NONE) || (g_rfid_mode == RFID_RT_CONTINUOUS)) {
        if (!check_and_create_new_daily_file(now)) {
            // handle shutdown in STATE_ENDOFLIFE
            state = STATE_ENDOFLIFE;
            return;
        }
        LOG_DEBUG("Woke up with events=0x%02X", events);
    }

    // ===== Periodic full acquisition (RTC driven) =====

    // If we woke up due to RTC, perform a full periodic acquisition and log.
    if (events & DEPLOY_EVT_RTC_WAKE) {
        LOG_DEBUG("RTC wake-up event.");
        rtc_clear_alarm_flag();
        set_next_deploy_alarm(now, in_awake_window);

        // Reserved for future use: read all sensors and log a full SensorFrame.
        // SensorFrame frame = readAllSensors(g_sensors, G_SENSOR_COUNT);
        // logSensorFrame(now, frame);

        if (config.enable_vbat && battery_is_available()) {
            vbat_counter++;

            if (vbat_counter >= VBAT_CHECK_INTERVAL) {
                vbat_counter = 0;

                if (!battery_service_read_vbat_filtered_mv(vbat_mv, changed)) {
                    LOG_ERROR("Failed to read filtered VBAT value");
                    // handle shutdown in STATE_ENDOFLIFE
                    state = STATE_ENDOFLIFE;
                    return;
                } else if (changed) {
                    LOG_INFO("VBAT changed: %d mV", vbat_mv);
                    logMeasurement(now, "VBAT", (float)vbat_mv, "mV", config.use_buffer);
                }

                if (!battery_service_decision("Deployment", vbat_mv)) {
                    error_signal(ERR_BATTERY_CRITICAL);
                    log_flush();
                    // handle shutdown in STATE_ENDOFLIFE
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

    if (events & (DEPLOY_EVT_BUTTON_PRESS | DEPLOY_EVT_BUTTON_RELEASE)) {
        LOG_INFO("Button event detected");
        deploy_handle_power_button(state, ir_driver, button_irq);
    }

    // ===== RFID trigger handling =====

    const bool rfid_continuous  = (g_rfid_mode == RFID_RT_CONTINUOUS);
    const bool rfid_on_ir_event = (g_rfid_mode == RFID_RT_ON_IR_EVENT);
    const bool ir_event = (events & DEPLOY_EVT_SENSOR_IR1) || (events & DEPLOY_EVT_SENSOR_IR2);

    rfid_trigger = rfid_continuous || (rfid_on_ir_event && ir_event);

    // Switch on RFID if needed.
    if (rfid_trigger && !pwr_manager::rfid_is_on()) {
        pwr_manager::rfid_pwr_on(g_rfid_mode);
        rfid_start_time     = millis();
        g_rfid_tag_detected = false;
        rfid_driver::poll_now(&rfid_driver);
    }
    // prolong RFID active time if already on and another IR event occurs.
    if (rfid_trigger) {
        g_rfid_triggered_ms = millis();
        g_rfid_deadline_ms  = g_rfid_triggered_ms + RFID_MAX_ACTIVE_MS;
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
            now_ms  = millis();
            elapsed = now_ms - rfid_start_time;
            if (g_rfid_tag_detected || now_ms >= g_rfid_deadline_ms) {
                pwr_manager::rfid_pwr_off(g_rfid_mode);

                g_rfid_tag_detected = false;
                g_rfid_triggered_ms = 0;
                g_rfid_deadline_ms  = 0;
            }
            LOG_DEBUG("RFID active for %d ms, tag detected: %d, deadline in %d ms", elapsed,
                      g_rfid_tag_detected, g_rfid_deadline_ms - now_ms);
        }
    }
}
