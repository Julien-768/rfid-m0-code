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
#include "sensors_internal.h"  // G_SENSOR_COUNT, g_sensors
#include "sd_manager.h"
#include "rtc.h"
#include "battery_service.h"
#include "config.h"
#include "log.h"
#include "error_handler.h"
#include "rfid_driver.h"
#include <ArduinoLowPower.h>
#include "utils.h"  // check_and_create_new_daily_file

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

// Global event flags set by ISRs and checked in the main loop.
static volatile uint8_t g_deploy_events = DEPLOY_EVT_NONE;

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

// RFID runtime state
static tag_info_t g_last_rfid_tag = {{0}, 0};
static bool g_has_last_rfid_tag   = false;

// RFID timing
constexpr uint32_t RFID_ACTIVE_WINDOW_MS = 80;
constexpr uint32_t RFID_DEBOUNCE_MS      = 1000;

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
 * @brief Initialize DEPLOY mode runtime and callbacks.
 *
 * Must be called once when entering DEPLOY state.
 *
 * @param ir_driver Reference to the IR PWM driver.
 * @param rfid Reference to the RFID driver.
 */
void deploy_enter(ir_pwm& ir_driver, rfid_driver_t& rfid) {
    LOG_DEBUG("Entering DEPLOY mode: setting up callbacks and initial state");
    log_flush();  // Before noInterrupts to ensure all logs are flushed before potential sleep
    noInterrupts();

    // Reset event flags and counters (ISR-related)
    g_deploy_events = DEPLOY_EVT_NONE;
    g_ir1_count     = 0;
    g_ir2_count     = 0;
    g_ir1_last_ts   = 0;
    g_ir2_last_ts   = 0;

    interrupts();

    // Reset runtime state
    vbat_counter        = 0;
    rtc_period          = config.acquisition_interval_s;
    g_last_rfid_tag     = {{0}, 0};
    g_has_last_rfid_tag = false;

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
 * @param rfid Reference to the RFID driver.
 */
void deploy_exit(ir_pwm& ir_driver, rfid_driver_t& rfid) {
    LOG_DEBUG("Exiting DEPLOY mode: clearing callbacks and state");
    log_flush();
    noInterrupts();

    // Reset event flags and counters (ISR-related)
    g_deploy_events = DEPLOY_EVT_NONE;
    g_ir1_count     = 0;
    g_ir2_count     = 0;
    g_ir1_last_ts   = 0;
    g_ir2_last_ts   = 0;

    interrupts();

    ir_driver.set_callback_sensor_1(nullptr);
    ir_driver.set_callback_sensor_2(nullptr);
    rtc_clear_alarm_flag();
    rtc_set_alarm_callback(nullptr);

    // Reset runtime state
    vbat_counter        = 0;
    rtc_period          = 0;
    g_last_rfid_tag     = {{0}, 0};
    g_has_last_rfid_tag = false;
}

/**
 * @brief Main DEPLOY state handler.
 *
 * This function implements one full iteration of the DEPLOY state.
 *
 * @param state Reference to the current system state. May be set to
 * @ref STATE_ENDOFLIFE by the battery check or other subsystems.
 * @param ir_driver Reference to the IR PWM driver.
 * @param rfid Reference to the RFID driver.
 */
void run_deploy_state(SystemState& state, rfid_driver_t& rfid) {
    uint8_t events     = DEPLOY_EVT_NONE;
    uint32_t ir1_count = 0;
    uint32_t ir2_count = 0;
    int32_t vbat_mv    = 0;
    bool changed       = false;
    DateTime now;

    // Enter low-power sleep; RTC alarm or sensor interrupt will wake the MCU.
    LowPower.sleep();

    log_flush();  // Flush any pending logs before processing events
    noInterrupts();
    events          = g_deploy_events;
    g_deploy_events = DEPLOY_EVT_NONE;

    ir1_count   = g_ir1_count;
    g_ir1_count = 0;

    ir2_count   = g_ir2_count;
    g_ir2_count = 0;
    interrupts();

    // If wake-up was not caused by the RTC alarm or a sensor interrupt, exit early.
    if (events == DEPLOY_EVT_NONE) return;

    // Common post-wake handling
    now = rtc().now();
    // LOG_DEBUG("Woke up from sleep with events: 0x%02X", events);
    if (!check_and_create_new_daily_file(now)) {
        state = STATE_ENDOFLIFE;
        return;
    }

    // ===== Periodic full acquisition (RTC driven) =====
    if (events & DEPLOY_EVT_RTC_WAKE) {
        // Clear alarm and program next wake-up.
        rtc_clear_and_set_alarm(now, rtc_period);

        LOG_DEBUG("RTC wake-up event. Scheduled next wake-up in %d seconds", rtc_period);

        // Read all active sensors.
        SensorFrame frame = readAllSensors(g_sensors, G_SENSOR_COUNT);

        // Log all sensor readings (AS7341, TSL2591, VBAT, etc.).
        logSensorFrame(now, frame);

        if (config.enable_vbat && battery_is_available()) {
            vbat_counter++;

            // Perform battery check every 10 RTC wakes.
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
                    // Handle decision failure
                    error_signal(ERR_BATTERY_CRITICAL);
                    state = STATE_ENDOFLIFE;
                    return;
                }
            }
        }
    }
    // Handle RFID events

    if (config.enable_rfid) {
        rfid_driver_force_poll(&rfid);

        const uint32_t t0 = millis();
        while ((uint32_t)(millis() - t0) < RFID_ACTIVE_WINDOW_MS) {
            rfid_driver_tick(&rfid);

            tag_info_t tag;
            while (rfid_driver_get_tag(&rfid, &tag)) {
                bool should_log = true;

                if (g_has_last_rfid_tag) {
                    should_log = rfid_should_record_tag(&g_last_rfid_tag, &tag, RFID_DEBOUNCE_MS);
                }

                if (should_log) {
                    LOG_INFO("RFID tag detected: %s", tag.tag);

                    // selon ton système de log:
                    logMeasurement(now, "RFID_TAG", 1.0f, tag.tag, config.use_buffer);
                    // ou mieux: fonction dédiée texte
                    // logText(now, "RFID_TAG", tag.tag, config.use_buffer);

                    g_last_rfid_tag     = tag;
                    g_has_last_rfid_tag = true;
                }
            }
            delay(2);
        }
    }

    // ===== Event-driven partial acquisition =====

    if (events & DEPLOY_EVT_SENSOR_AS7341) {
        // For future use: implement partial AS7341 acquisition on interrupt wake-up.
    }

    if (events & DEPLOY_EVT_SENSOR_TSL2591) {
        // For future use: implement partial TSL2591 acquisition on interrupt wake-up.
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
