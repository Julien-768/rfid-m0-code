/**
 * @file rtc.cpp
 * @defgroup rtc_manager RTC Manager
 * @ingroup system_modules
 * @brief DS3231 RTC management and wake-up scheduling.
 *
 * This module provides RTC services:
 * - DS3231 initialization and basic health checks
 * - Alarm1 scheduling for periodic wake-up
 * - Wake-up interrupt wiring to SAMD21 low-power sleep
 * - Daily log file rollover helper
 * - Boot-time time sanity checks and recovery strategy
 * - Internal RTC ISR with configurable user callback
 *
 * ## Ownership and dependencies
 * - The I2C bus (`Wire`) must be initialized elsewhere (e.g., in `setup()`).
 *   This module does **not** call `Wire.begin()`.
 * - Sleep/wake is implemented via ArduinoLowPower + an interrupt pin.
 *
 * ## Time trust model
 * At boot, the RTC time is validated against basic sanity rules and firmware build
 * time. If time is invalid:
 * - Otherwise: it falls back to firmware build time.
 *
 * @warning Any direct manipulation of SAMD21 EIC registers is intentionally avoided
 *          here. ArduinoLowPower already resolves the correct EXTINT line and enables
 *          wake-up on supported SAMD cores.
 *
 * @see rtc.h
 * @see ArduinoLowPower
 * @see RTClib
 * @{
 */

#include "rtc.h"

#include <ArduinoLowPower.h>
#include <RTClib.h>
#include <Wire.h>
#include <string.h>

#include "error_handler.h"
#include "log.h"
#include "hardware.h"
#include "utils.h"

/**
 * @brief Global DS3231 instance (RTClib).
 */
RTC_DS3231 rtc_instance;

/**
 * @brief Return the global RTC instance.
 *
 * @return Reference to the global RTC object.
 */
RTC_DS3231& get_rtc() {
    return rtc_instance;
}

/**
 * @brief Global RTC runtime state.
 */
RTC_STATE rtc_state_instance;

/**
 * @brief Return the global RTC runtime state.
 *
 * @return Reference to the global RTC runtime state.
 */
RTC_STATE& get_rtc_state() {
    return rtc_state_instance;
}

/**
 * @brief Alarm flag set by the RTC interrupt when Alarm1 triggers.
 *
 * Marked volatile because it is written from interrupt context.
 */
static volatile bool s_alarm_flag = false;

/**
 * @brief Internal flag to track if RTC initialization has been performed.
 */
static bool s_rtc_initialized = false;

/**
 * @brief Return true if the RTC has been initialized.
 *
 * @return true if the RTC is initialized, false otherwise.
 */
bool rtc_is_initialized() {
    return s_rtc_initialized;
}

/**
 * @brief Optional user callback executed from the RTC ISR.
 *
 * @note This callback runs in interrupt context and must stay short
 *       and non-blocking.
 */
static rtc_alarm_callback_t s_alarm_callback = nullptr;

//-----------------------------------------------------------------------------

static bool isDST(int year, int month, int day) {
    // simple approximation Europe
    if (month < 3 || month > 10) return false;
    if (month > 3 && month < 10) return true;

    int lastSunday = day - ((day + 6) % 7);

    if (month == 3) return lastSunday >= 25;
    if (month == 10) return lastSunday < 25;

    return false;
}

// -----------------------------------------------------------------------------
// Interrupt + wake-up
// -----------------------------------------------------------------------------

/**
 * @brief Internal RTC alarm ISR.
 *
 * This ISR is attached to the DS3231 INT/SQW pin configured for alarm
 * interrupts. It sets the internal software flag and optionally invokes
 * a user-provided callback.
 */
static void rtc_alarm_isr() {
    s_alarm_flag = true;

    if (s_alarm_callback != nullptr) {
        s_alarm_callback();
    }
}

/**
 * @brief Register or replace the user callback executed by the RTC ISR.
 *
 * @param callback Function called from interrupt context when the RTC alarm
 *                 fires. Pass nullptr to disable the callback.
 */
void rtc_set_alarm_callback(rtc_alarm_callback_t callback) {
    s_alarm_callback = callback;
}

/**
 * @brief Return true if the RTC alarm has fired since the last clear.
 *
 * @return true if an alarm interrupt occurred, false otherwise.
 */
bool rtc_alarm_fired() {
    RTC_DS3231& rtc = get_rtc();
    LOG_DEBUG("Checking RTC alarm flag");
    s_alarm_flag = rtc.alarmFired(DS3231_ALARM_1);
    LOG_DEBUG("RTC alarm flag is %s", s_alarm_flag ? "SET" : "NOT SET");
    return s_alarm_flag;
}

/**
 * @brief Configure a GPIO interrupt as a wake-up source for the RTC alarm.
 *
 * This should be called after scheduling an alarm with
 * @ref rtc_clear_and_set_alarm().
 *
 * @param interrupt_pin Arduino pin number connected to the DS3231 INT/SQW output.
 * @param isr ISR to execute when the alarm line triggers.
 *
 * @note The DS3231 INT/SQW output is typically active low and should be wired
 *       with a pull-up.
 * @note This implementation relies on ArduinoLowPower, which already resolves
 *       the correct EXTINT line using `g_APinDescription[pin].ulExtInt`.
 */
void rtc_configure_interrupt(uint8_t interrupt_pin, void (*isr)()) {
    pinMode(interrupt_pin, INPUT_PULLUP);
    LowPower.attachInterruptWakeup(interrupt_pin, isr, FALLING);
    LOG_DEBUG("RTC interrupt pin=%d digitalPinToInterrupt=%d extint=%d",
              interrupt_pin,
              digitalPinToInterrupt(interrupt_pin),
              g_APinDescription[interrupt_pin].ulExtInt);
}

// -----------------------------------------------------------------------------
// Init / status
// -----------------------------------------------------------------------------

/**
 * @brief Initialize the DS3231 RTC and configure the alarm wake-up interrupt.
 *
 * - Checks that the I2C bus is ready
 * - Calls `rtc().begin()` (RTClib)
 * - Logs lost-power status
 * - Disables SQW output and clears alarm flags
 * - Attaches the internal RTC ISR to the RTC interrupt pin
 *
 * @param interrupt_pin Arduino pin connected to the DS3231 INT/SQW output.
 * @return true on success, false on failure.
 *
 * @warning `Wire.begin()` must have been called before this function.
 */
bool rtc_initialization(uint32_t interrupt_pin) {
    RTC_DS3231& rtc = get_rtc();

    if (!rtc.begin()) {
        error_signal(ERR_RTC_FAILURE);
        return false;
    }

    s_rtc_initialized = true;

    if (rtc.lostPower()) {
        LOG_WARN("RTC lost power, needs reconfiguration via SET_CONFIG");
    }

    rtc.writeSqwPinMode(DS3231_OFF);
    rtc.clearAlarm(DS3231_ALARM_1);
    rtc.clearAlarm(DS3231_ALARM_2);

    // Attach the internal ISR used by this module.
    rtc_configure_interrupt(static_cast<uint8_t>(interrupt_pin), rtc_alarm_isr);

    LOG_INFO("RTC initialized");
    return true;
}

/**
 * @brief Clear the software alarm flag and acknowledge Alarm1 on the DS3231.
 *
 * This must be called after waking up from a DS3231 alarm to prevent repeated
 * wake-ups while the alarm line remains asserted.
 */
void rtc_clear_alarm_flag() {
    RTC_DS3231& rtc = get_rtc();

    s_alarm_flag = false;
    LOG_DEBUG("Clearing RTC alarm flag and acknowledging DS3231 Alarm1");
    rtc.clearAlarm(DS3231_ALARM_1);
}

/**
 * @brief Probe the I2C bus to check if a DS3231 responds at address 0x68.
 *
 * @return true if the device ACKs, false otherwise.
 *
 * @warning Requires `Wire.begin()` to have been called.
 */
bool scan_i2c_for_ds3231() {
    const uint8_t ds3231_address = 0x68;
    Wire.beginTransmission(ds3231_address);
    return (Wire.endTransmission() == 0);
}

// -----------------------------------------------------------------------------
// Wake scheduling
// -----------------------------------------------------------------------------

/**
 * @brief Schedule the next RTC wake-up using DS3231 Alarm1.
 *
 * The scheduling strategy is:
 * - round the current time down to the minute boundary (seconds = 0)
 * - add @p interval_s
 * - program Alarm1
 *
 * @param now Current RTC time.
 * @param interval_s Wake-up interval in seconds.
 *
 * @note Alarm mode is set to `DS3231_A1_Minute`, which matches on minute
 *       boundaries according to RTClib's DS3231 alarm behavior. If sub-minute
 *       timing is required, use a different alarm mode and strategy.
 */
void rtc_clear_and_set_alarm(const DateTime& now, uint32_t interval_s) {
    RTC_DS3231& rtc = get_rtc();

    // Round to minute boundary, then add the requested interval.
    DateTime rounded(now.year(), now.month(), now.day(), now.hour(), now.minute(), 0);
    DateTime wakeup = rounded + TimeSpan(interval_s);

    rtc.clearAlarm(DS3231_ALARM_1);
    rtc.setAlarm1(wakeup, DS3231_A1_Second);
}

/**
 * @brief Program Alarm1 to trigger at an absolute DateTime.
 *
 * This function sets DS3231 Alarm1 to fire at the specified time.
 * Seconds are ignored because DS3231_A1_Minute mode is used.
 *
 * @param when Absolute time for the next wake-up.
 */
void rtc_set_alarm_at(const DateTime& when) {
    RTC_DS3231& rtc = get_rtc();

    // IMPORTANT:
    // Ton code actuel utilise DS3231_A1_Minute → résolution à la minute.
    // Donc on force les secondes à 0 pour cohérence.
    DateTime aligned(when.year(), when.month(), when.day(), when.hour(), when.minute(), 0);

    rtc.clearAlarm(DS3231_ALARM_1);
    rtc.setAlarm1(aligned, DS3231_A1_Second);

    // Optionnel mais fortement recommandé pour debug terrain
    LOG_DEBUG("RTC alarm set at %04d-%02d-%02d %02d:%02d:00",
              aligned.year(),
              aligned.month(),
              aligned.day(),
              aligned.hour(),
              aligned.minute());
}

// -----------------------------------------------------------------------------
// RTC sanity / boot recovery
// -----------------------------------------------------------------------------

/**
 * @brief Basic RTC sanity check.
 *
 * Checks that:
 * - year must be within [2020, 2099]
 * - RTC time must not be earlier than firmware build time
 *
 * @param now Current RTC time.
 * @param build Firmware build timestamp.
 * @return true if RTC time is considered sane, false otherwise.
 */
static bool rtc_sanity_ok(const DateTime& now, const DateTime& build) {
    if (now.year() < 2020 || now.year() > 2099) {
        return false;
    }

    if (now < build) {
        return false;
    }

    return true;
}

/**
 * @brief Boot-time RTC recovery strategy (trust / fallback).
 *
 * This function implements the following logic:
 * - If RTC time is sane at boot: accept it
 * - Otherwise: fallback to firmware build time
 *
 * @return true if the RTC policy completed successfully
 */
bool rtc_boot_recover() {
    RTC_DS3231& rtc      = get_rtc();
    RTC_STATE& rtc_state = get_rtc_state();

    const DateTime build_time(F(__DATE__), F(__TIME__));

    // Calculate local build time by applying a DST offset to the build time.
    int offset = isDST(build_time.year(), build_time.month(), build_time.day()) ? 2 : 1;
    DateTime build_time_utc = build_time - TimeSpan(0, offset, 0, 0);
    const DateTime now      = rtc.now();
    IsoFormatOptions opts;
    opts.separator           = " ";
    String now_string        = isoformat(now, {opts});
    String build_time_string = isoformat(build_time_utc, {opts});

    // TODO should use a format converter from 'utils.cpp' instead of individual field accessors
    LOG_DEBUG("RTC boot check: rtc=%s, build=%s, lostPower=%s",
              now_string.c_str(),
              build_time_string.c_str(),
              rtc.lostPower() ? "YES" : "NO");

    const bool time_in_range = rtc_sanity_ok(now, build_time_utc);

    rtc_state.time_checked           = !rtc.lostPower() && time_in_range;
    rtc_state.time_in_connected_mode = 0;
    if (rtc_state.time_checked) {
        LOG_INFO("RTC time accepted as valid at boot: %s", now_string.c_str());
    } else {
        rtc.adjust(build_time_utc);
        rtc_state.time_checked = true;
        LOG_WARN("RTC time invalid at boot, setting to build time fallback: %s",
                 build_time_string.c_str());
    }

    return true;
}

/**
 * @brief Apply time provided by an external GUI and finalize time verification.
 *
 * - Updates DS3231 time using `rtc().adjust(t)`
 * - Marks time as verified
 * - Clears any waiting deadline
 *
 * @param t External trusted time to apply.
 */
void rtc_apply_external_time(const DateTime& t) {
    RTC_DS3231& rtc      = get_rtc();
    RTC_STATE& rtc_state = get_rtc_state();

    rtc.adjust(t);
    rtc_state.time_checked           = true;
    rtc_state.time_in_connected_mode = 0;

    LOG_INFO("RTC updated from GUI (SET_CONFIG)");
}

/** @} */  // end of rtc_manager group
