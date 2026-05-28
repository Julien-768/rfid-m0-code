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
#include "signal.h"
#include "irq_helper.h"

extern Uart SerialAlt;

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

// RFID power sequencing state.
// This prevents repeated power ON/OFF cycles while the reader is still booting.
static bool g_rfid_requested    = false;
static bool g_rfid_waiting_boot = false;

static rfid_runtime_mode g_rfid_mode = RFID_RT_DISABLED;

// RFID timing
constexpr uint32_t RFID_CONTINUOUS_WINDOW_MS = 10;
constexpr uint32_t RFID_DEBOUNCE_MS          = 1000;
constexpr uint32_t RFID_MIN_ACTIVE_MS        = 2000;
constexpr uint32_t RFID_MAX_ACTIVE_MS        = 5000;
// TODO RFID_BOOT_DELAY_MS
constexpr uint32_t RFID_BOOT_DELAY_MS = 0;

// Track whether external sensor wakeups are currently enabled.
static bool g_sensor_wakeups_enabled = false;

static void log_user_battery_check() {
    int32_t vbat_mv = 0;
    bool changed    = false;

    LOG_INFO("Short power-button press: battery check requested");
    if (config.enable_vbat && battery_is_available() &&
        battery_service_read_vbat_filtered_mv(vbat_mv, changed)) {
        if (!logMeasurement(rtc().now(),
                            "VBAT_USER_CHECK",
                            (float)vbat_mv,
                            "mV",
                            config.use_buffer)) {
            LOG_ERROR("User battery check logging failed");
        }
    }
}

static void deploy_handle_power_button(SystemState& state) {
    pwr_manager::power_button_event_t button_event = {};

    if (!pwr_manager::consume_power_button_event(button_event)) {
        return;
    }

    LOG_INFO("Button pressed for %lu ms", button_event.duration_ms);

    if (button_event.type == pwr_manager::POWER_BUTTON_LONG_PRESS) {
        LOG_WARN("Long power-button press: user shutdown requested");
        if (!logMeasurement(rtc().now(), "SHUTDOWN_USER", 1.0f, "count", config.use_buffer)) {
            LOG_ERROR("Shutdown logging failed");
        }
        log_flush();
        state = STATE_ENDOFLIFE;
        pwr_manager::reset_power_button_tracking();
        return;
    }

    if (button_event.type == pwr_manager::POWER_BUTTON_SHORT_PRESS) {
        log_user_battery_check();
        blink_blocking_safe(PIN_BUZZER_LED, 100, 100, 2);
    }
}

// Example schedule: active from 08:00 to 18:30 daily.
static ScheduleManager g_schedule({
    8,
    0,  // start 08:00
    18,
    30  // end 18:30
});

static volatile uint8_t g_ir1_state = LOW;
static volatile uint8_t g_ir2_state = LOW;

// ===== Interrupt Service Routines =====

static void callback_rtc() {
    g_deploy_events |= DEPLOY_EVT_RTC_WAKE;
}

static void callback_ir1(uint8_t /*unused_state*/) {
    const uint32_t now = millis();

    if ((now - g_ir1_last_ts) < IR_DEBOUNCE_MS) {
        return;
    }

    g_ir1_state = digitalRead(PIN_PR_1);

    g_ir1_last_ts = now;
    g_ir1_count++;
    g_deploy_events |= DEPLOY_EVT_SENSOR_IR1;
}

static void callback_ir2(uint8_t /*unused_state*/) {
    const uint32_t now = millis();

    if ((now - g_ir2_last_ts) < IR_DEBOUNCE_MS) {
        return;
    }

    g_ir2_state = digitalRead(PIN_PR_2);

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

static void callback_button() {
    pwr_manager::power_button_irq_handler();
    if (pwr_manager::power_button_read_state()) {
        g_deploy_events |= DEPLOY_EVT_BUTTON_PRESS;
    } else {
        g_deploy_events |= DEPLOY_EVT_BUTTON_RELEASE;
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

static uint32_t rfid_start_time = 0;
static uint32_t now_ms          = 0;
static uint32_t elapsed         = 0;

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

    LOG_DEBUG("ENTER ACTIVE: mode=%u, is_on_before=%d",
              (uint8_t)rfid_rt_mode,
              pwr_manager::rfid_is_on());

    if (rfid_rt_mode == RFID_RT_CONTINUOUS) {
        LOG_DEBUG("RFID continuous: power ON and arm polling");

        pwr_manager::rfid_pwr_on(rfid_rt_mode);

        g_rfid_requested    = true;
        g_rfid_waiting_boot = true;
        g_rfid_tag_detected = false;
        rfid_start_time     = millis();
        LOG_DEBUG("RFID continuous armed: is_on_after=%d, requested=%d, waiting_boot=%d, start=%lu",
                  pwr_manager::rfid_is_on(),
                  g_rfid_requested,
                  g_rfid_waiting_boot,
                  rfid_start_time);
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
    g_rfid_requested    = false;
    g_rfid_waiting_boot = false;

    if (ir_enabled) {

        ir_driver.stop_pwm();
        if (g_sensor_wakeups_enabled) {
            ir_driver.disable_sensor_wakeups();
            g_sensor_wakeups_enabled = false;
        }
        pwr_manager::ir_power_off();
    }
}

static void apply_awake_window(ir_pwm& ir_driver,
                               const DateTime& now,
                               bool& in_awake_window,
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

    g_schedule.setWindow({8, 0, 18, 0});
    rtc_period   = config.acquisition_interval_s;
    vbat_counter = 0;

    g_rfid_mode     = (rfid_runtime_mode)config.rfid_mode;
    in_awake_window = false;

    g_last_rfid_tag          = {{0}, 0};
    g_has_last_rfid_tag      = false;
    g_rfid_tag_detected      = false;
    g_rfid_triggered_ms      = 0;
    g_rfid_deadline_ms       = 0;
    g_rfid_requested         = false;
    g_rfid_waiting_boot      = false;
    g_sensor_wakeups_enabled = false;
    log_flush();

    LOG_DEBUG("Checking initial schedule window at startup");
    rtc_clear_alarm_flag();
    delay(100);

    DateTime now = rtc().now();
    apply_awake_window(ir_driver, now, in_awake_window, g_rfid_mode);

    noInterrupts();

    g_deploy_events  = DEPLOY_EVT_NONE;
    g_ir1_count      = 0;
    g_ir2_count      = 0;
    g_ir1_last_ts    = 0;
    g_ir2_last_ts    = 0;
    g_ir1_last_state = digitalRead(PIN_PR_1);
    g_ir2_last_state = digitalRead(PIN_PR_2);

    interrupts();

    LOG_DEBUG("Setting up DEPLOY mode callbacks");
    pwr_manager::reset_power_button_tracking();

    rtc_set_alarm_callback(callback_rtc);
    set_next_deploy_alarm(now, in_awake_window);

    ir_driver.set_callback_sensor_1(callback_ir1);
    ir_driver.set_callback_sensor_2(callback_ir2);
}

static bool rtc_is_present() {
    Wire.beginTransmission(0x68);
    return Wire.endTransmission() == 0;
}

/**
 * @brief Cleanup DEPLOY mode runtime and callbacks.
 */
void deploy_exit(ir_pwm& ir_driver) {
    LOG_DEBUG("Exiting DEPLOY mode: clearing callbacks and state");
    log_flush();
    noInterrupts();

    g_deploy_events  = DEPLOY_EVT_NONE;
    g_ir1_count      = 0;
    g_ir2_count      = 0;
    g_ir1_last_ts    = 0;
    g_ir2_last_ts    = 0;
    g_ir1_last_state = HIGH;
    g_ir2_last_state = HIGH;

    interrupts();

    LOG_DEBUG("clear RTC alarm callback");
    low_power_detach_interrupt(RTC_INTERRUPT_PIN);

    rtc_set_alarm_callback(nullptr);
    if (rtc_is_initialized()) {
        rtc_clear_alarm_flag();
    } else {
        LOG_WARN("skip rtc_clear_alarm_flag: RTC not initialized");
    }

    // LOG_DEBUG("detach interrupt for power button");
    // pwr_manager::button_interrupt_detach();

    LOG_DEBUG("disable IR wakeups, PWM, 3V rail, 5V rail");
    deploy_leave_active_window(ir_driver);
    ir_driver.set_callback_sensor_1(nullptr);
    ir_driver.set_callback_sensor_2(nullptr);

    vbat_counter             = 0;
    rtc_period               = 0;
    g_last_rfid_tag          = {{0}, 0};
    g_has_last_rfid_tag      = false;
    g_rfid_tag_detected      = false;
    g_rfid_triggered_ms      = 0;
    g_rfid_deadline_ms       = 0;
    g_rfid_requested         = false;
    g_rfid_waiting_boot      = false;
    in_awake_window          = false;
    g_sensor_wakeups_enabled = false;
    g_rfid_mode              = RFID_RT_DISABLED;

    LOG_DEBUG("Exiting DEPLOY mode: callbacks cleared and state reset");
}

int count_4_dot = 0;

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

    uint8_t ir1_state = LOW;
    uint8_t ir2_state = LOW;

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
        // for (int i = 0; i < 20; i++) {
        //     // signal_engine_update();
        //     delay(10);
        // }

        // LOG_DEBUG("After wake: rtc_irq=%d (0 means active irq, 1 means no irq), events=0x%02X",
        //           digitalRead(10), g_deploy_events);
    }

    pwr_manager::power_button_poll();

    if (pwr_manager::power_button_event_pending()) {
        g_deploy_events |= DEPLOY_EVT_BUTTON_RELEASE;
    }

    noInterrupts();

    events = g_deploy_events;
    g_deploy_events &= ~events;

    ir1_count   = g_ir1_count;
    g_ir1_count = 0;

    ir2_count   = g_ir2_count;
    g_ir2_count = 0;

    ir1_state = g_ir1_state;
    ir2_state = g_ir2_state;

    interrupts();

    // Outside active window, ignore non-RTC events defensively.
    now = rtc().now();
    apply_awake_window(ir_driver, now, in_awake_window, g_rfid_mode);

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
        //         if (count_4_dot < 4) {
        // #if (LOG_SERIAL_OUTPUT == LOG_SERIAL1)
        //             Serial1.print(".");
        // #endif
        // #if (LOG_SERIAL_OUTPUT == LOG_ALT_SERIAL)
        //             SerialAlt.print(".");
        // #endif
        //             count_4_dot++;
        //         }
        //         if (count_4_dot >= 4) {
        // #if (LOG_SERIAL_OUTPUT == LOG_SERIAL1)
        //             Serial1.println(".");
        // #endif
        // #if (LOG_SERIAL_OUTPUT == LOG_ALT_SERIAL)
        //             SerialAlt.println(".");
        // #endif
        //             count_4_dot = 0;
        //         }
    }

    // ===== RFID trigger handling =====

    const bool rfid_continuous  = (g_rfid_mode == RFID_RT_CONTINUOUS);
    const bool rfid_on_ir_event = (g_rfid_mode == RFID_RT_ON_IR_EVENT);
    const bool ir_event = (events & DEPLOY_EVT_SENSOR_IR1) || (events & DEPLOY_EVT_SENSOR_IR2);

    rfid_trigger = rfid_on_ir_event && ir_event;

    // Switch on RFID if needed.
    // Use g_rfid_requested as a software latch so repeated IR events cannot
    // restart the RFID reader while it is still booting.
    if (rfid_trigger && !g_rfid_requested) {
        LOG_DEBUG("RFID trigger from IR event: power ON");
        g_rfid_requested    = true;
        g_rfid_waiting_boot = true;

        if (!pwr_manager::rfid_is_on()) {
            LOG_DEBUG("RFID power ON");
            pwr_manager::rfid_pwr_on(g_rfid_mode);
        } else {
            LOG_DEBUG("RFID already powered ON");
        }

        rfid_start_time     = millis();
        g_rfid_tag_detected = false;

        // Drop any bytes already present before the reader boot sequence.
        rfid_driver::flush_rx(&rfid_driver);
    }
    // prolong RFID active time if already on and another IR event occurs.
    if (rfid_trigger) {
        g_rfid_triggered_ms = millis();
        g_rfid_deadline_ms  = g_rfid_triggered_ms + RFID_MAX_ACTIVE_MS;
    }

    // ===== Common post-wake handling =====
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
                    if (!logMeasurement(now, "VBAT", (float)vbat_mv, "mV", config.use_buffer)) {
                        LOG_ERROR("VBAT measurement logging failed");
                    }
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

    if (events & (DEPLOY_EVT_BUTTON_PRESS | DEPLOY_EVT_BUTTON_RELEASE)) {
        LOG_INFO("Button event detected");
        deploy_handle_power_button(state);
    }

    if (events & DEPLOY_EVT_SENSOR_IR1) {
        const char* ir1_label = ir1_state ? "IR1_ON" : "IR1_OFF";

        LOG_INFO("%s event detected (count: %d)", ir1_label, ir1_count);

        if (!logMeasurement(now, ir1_label, (float)ir1_count, "count", config.use_buffer)) {
            LOG_ERROR("%s event logging failed", ir1_label);
        }
    }

    if (events & DEPLOY_EVT_SENSOR_IR2) {
        const char* ir2_label = ir2_state ? "IR2_ON" : "IR2_OFF";

        LOG_INFO("%s event detected (count: %d)", ir2_label, ir2_count);

        if (!logMeasurement(now, ir2_label, (float)ir2_count, "count", config.use_buffer)) {
            LOG_ERROR("%s event logging failed", ir2_label);
        }
    }

    // ===== RFID acquisition =====

    // Logical polling state.
    // Hardware power state alone must not control polling,
    // otherwise polling can continue forever if power-off is skipped.
    const bool should_poll =
        pwr_manager::rfid_is_on() && (rfid_continuous || g_rfid_requested || g_rfid_waiting_boot);
    // LOG_DEBUG(
    //     "RFID POLL CHECK: is_on=%d, continuous=%d, trigger=%d, requested=%d, waiting_boot=%d, "
    //     "should_poll=%d",
    //     pwr_manager::rfid_is_on(),
    //     rfid_continuous,
    //     rfid_trigger,
    //     g_rfid_requested,
    //     g_rfid_waiting_boot,
    //     should_poll);

    if (should_poll) {
        // Do not poll during the reader boot window. Otherwise the boot banner
        // can be parsed as an invalid tag, and repeated IR events may look like
        // power cycling.
        if (g_rfid_requested && g_rfid_waiting_boot) {
            if ((uint32_t)(millis() - rfid_start_time) >= RFID_BOOT_DELAY_MS) {
                g_rfid_waiting_boot = false;

                // Remove boot banner / garbage, then let tick() send the first
                // command using the normal polling timer.
                LOG_INFO("RFID first poll after boot delay: %lu ms", millis() - rfid_start_time);
                rfid_driver::flush_rx(&rfid_driver);
                rfid_driver::poll_now(&rfid_driver);
            }
        } else {
            // Poll RFID driver and process and queue tags.
            rfid_driver::tick(&rfid_driver);
        }

        tag_info_t tag;

        // Process all available tags in the FIFO.
        while (rfid_driver::get_tag(&rfid_driver, &tag)) {
            LOG_DEBUG("Received RFID tag: %s (time since poll: %d ms)",
                      tag.tag,
                      millis() - tag.time_ms);

            bool should_log =
                !g_has_last_rfid_tag ||
                rfid_driver::should_record_tag(&g_last_rfid_tag, &tag, RFID_DEBOUNCE_MS);

            if (should_log) {
                LOG_INFO("RFID tag detected: %s", tag.tag);

                DateTime tag_now = rtc().now();

                if (!logMeasurement(tag_now, "RFID_TAG", 1.0f, tag.tag, config.use_buffer)) {
                    LOG_ERROR("RFID tag logging failed");
                }

                g_last_rfid_tag     = tag;
                g_has_last_rfid_tag = true;
                g_rfid_tag_detected = true;
            }
        }

        if (!rfid_continuous) {
            now_ms                        = millis();
            elapsed                       = now_ms - rfid_start_time;
            const bool min_active_reached = elapsed >= RFID_MIN_ACTIVE_MS;

            const bool should_power_off = (g_rfid_tag_detected && min_active_reached) ||
                                          ((int32_t)(now_ms - g_rfid_deadline_ms) >= 0);

            const int32_t deadline_left_ms = (int32_t)(g_rfid_deadline_ms - now_ms);

            LOG_DEBUG("RFID active for %lu ms, tag detected: %d, deadline in %ld ms",
                      elapsed,
                      g_rfid_tag_detected,
                      deadline_left_ms);

            if (should_power_off) {
                // Optional physical power OFF.
                // pwr_manager::rfid_pwr_off(g_rfid_mode);

                // Stop logical polling immediately.
                g_rfid_requested    = false;
                g_rfid_waiting_boot = false;
                g_rfid_tag_detected = false;
                g_rfid_triggered_ms = 0;
                g_rfid_deadline_ms  = 0;

                LOG_DEBUG("RFID polling stopped");
            }
        }
    }
}
