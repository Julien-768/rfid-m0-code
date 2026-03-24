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
 *
 * ## Ownership and dependencies
 * - The I2C bus (`Wire`) must be initialized elsewhere (e.g., in `setup()`).
 *   This module does **not** call `Wire.begin()`.
 * - Sleep/wake is implemented via ArduinoLowPower + an interrupt pin.
 *
 * ## Time trust model
 * At boot, the RTC time is validated against basic sanity rules and firmware build
 * time. If time is invalid:
 * - If the logger is connected to an external GUI: it enters a "waiting for time"
 *   window (see @ref rtc_boot_recover()).
 * - Otherwise: it falls back to firmware build time.
 *
 * @warning Any direct manipulation of SAMD21 EIC registers (e.g., `EIC->WAKEUP`)
 *          can be core-dependent and may break across platform/core updates.
 *
 * @see rtc.h
 * @see ArduinoLowPower
 * @see RTClib
 * @{
 */

#include "rtc.h"
#include <ArduinoLowPower.h>
#include <RTClib.h>
#include <string.h>
#include "error_handler.h"
#include "log.h"

/**
 * @brief Global DS3231 instance (RTClib).
 */
// Define the global RTC object
RTC_DS3231 rtc_instance;

// Implement the getter function
RTC_DS3231& get_rtc() {
    return rtc_instance;
}

/**
 * @brief Global RTC runtime state.
 *
 * Tracks whether time is currently trusted and when to stop waiting for GUI time.
 */
RTC_STATE rtc_state_instance;
// Implement the getter function
RTC_STATE& get_rtc_state() {
    return rtc_state_instance;
}

/**
 * @brief Alarm flag set by the RTC interrupt when Alarm1 triggers.
 *
 * Marked volatile as it is written from an ISR.
 */
static volatile bool s_alarm_flag = false;

/**
 * @brief RTC interrupt pin number (DS3231 INT/SQW).
 *
 * Default pin can be overridden by defining `RTC_INTERRUPT_PIN` in `rtc.h`.
 */
static uint8_t s_rtcInterruptPin = 0xFF;  // Invalid by default

// -----------------------------------------------------------------------------
// Interrupt + wake-up
// -----------------------------------------------------------------------------

/**
 * @brief Initialize the RTC interrupt pin.
 *
 * @param interruptPin The pin number to use for the RTC interrupt.
 */
void rtc_init_interrupt_pin(uint8_t interruptPin) {
    s_rtcInterruptPin = interruptPin;
    LOG_INFO("RTC interrupt pin set to %d.", s_rtcInterruptPin);
}

/**
 * @brief RTC alarm ISR: sets the internal alarm flag.
 *
 * This ISR is attached to the DS3231 INT/SQW pin configured for alarm interrupts.
 */
static void rtc_alarm_isr() {
    s_alarm_flag = true;
}

/**
 * @brief Configure the RTC interrupt pin and attach it as a wake-up source.
 *
 * - Configures the provided `interrupt_pin` as INPUT_PULLUP.
 * - Attaches an interrupt wake-up callback using ArduinoLowPower.
 * - Optionally enables the pin in the EIC WAKEUP register.
 *
 * @param interrupt_pin The pin number to use for the RTC interrupt.
 *
 * @warning The EIC WAKEUP register usage is low-level and may be fragile depending
 *          on the Arduino core version and pin mapping. If wake-up stops working
 *          after a core update, consider removing the direct EIC line.
 *
 * @see alarm_triggered()
 * @see clear_alarm_flag()
 */
void rtc_enable_wakeup_interrupt(uint8_t interrupt_pin = s_rtcInterruptPin) {
    pinMode(interrupt_pin, INPUT_PULLUP);
    LowPower.attachInterruptWakeup(digitalPinToInterrupt(interrupt_pin), rtc_alarm_isr, FALLING);

    // NOTE: depending on core, this direct EIC usage may be fragile.
    // Keep if it works for you; otherwise remove it.
    EIC->WAKEUP.reg |= (1 << interrupt_pin);
    while (EIC->STATUS.bit.SYNCBUSY)
        {}
}

// -----------------------------------------------------------------------------
// Init / status
// -----------------------------------------------------------------------------

/**
 * @brief Initialize the DS3231 RTC and clear alarm configuration.
 *
 * - Calls `rtc.begin()` (RTClib).
 * - Logs lost-power status (does not automatically set time here).
 * - Disables SQW output and clears Alarm1/Alarm2 flags.
 *
 * @return true on success, false on failure.
 *
 * @warning `Wire.begin()` must have been called before this function.
 * @see rtc_boot_recover()
 */
bool rtc_initialization() {
    // Wire.begin() should already be done before calling this
    // Check if the I2C bus is ready (without initializing it)
    Wire.beginTransmission(0x00);  // Dummy address to test bus availability
    uint8_t i2c_status = Wire.endTransmission();
    if (i2c_status != 0)
        {
            LOG_ERROR("I2C bus not ready (Wire not initialized or busy). Call Wire.begin() first.");
            error_signal(ERR_I2C_NOT_READY, false);
            return false;
    }

    if (!rtc.begin())
        {
            error_signal(ERR_RTC_FAILURE, false);
            return false;
    }

    if (rtc.lostPower())
        {
            LOG_WARN("RTC lost power, needs reconfiguration via SET_CONFIG");
    }

    rtc.writeSqwPinMode(DS3231_OFF);
    rtc.clearAlarm(DS3231_ALARM_1);
    rtc.clearAlarm(DS3231_ALARM_2);

    LOG_INFO("RTC initialized successfully");
    return true;
}

/**
 * @brief Check whether Alarm1 interrupt has fired since last clear.
 *
 * @return true if the alarm flag is set, false otherwise.
 *
 * @note This only reports the software flag (@ref s_alarm_flag). It does not read
 *       DS3231 alarm registers.
 */
bool alarm_triggered() {
    return s_alarm_flag;
}

/**
 * @brief Clear the software alarm flag and acknowledge Alarm1 on the DS3231.
 *
 * This must be called after waking up from a DS3231 alarm to prevent repeated wake-ups.
 *
 * @see alarm_triggered()
 */
void clear_alarm_flag() {
    s_alarm_flag = false;
    rtc.clearAlarm(DS3231_ALARM_1);
}

/**


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
 * The scheduling strategy:
 * - Round the current time down to the minute boundary (seconds=0),
 * - Add @p interval_s,
 * - Program Alarm1.
 *
 * @param now Current time (RTC).
 * @param interval_s Wake-up interval in seconds.
 *
 * @note Alarm mode is set to `DS3231_A1_Minute`, which matches on minute boundaries
 *       according to RTClib's DS3231 alarm behavior. Ensure this matches the desired
 *       resolution. If sub-minute timing is required, a different alarm mode and
 *       rounding strategy should be used.
 *
 * @see rtc_enable_wakeup_interrupt()
 */
void rtc_schedule_next_wake(const DateTime& now, uint16_t interval_s) {
    // Round to minute boundary, then add interval
    DateTime rounded(now.year(), now.month(), now.day(), now.hour(), now.minute(), 0);
    DateTime wakeup = rounded + TimeSpan(interval_s);

    rtc.clearAlarm(DS3231_ALARM_1);
    rtc.setAlarm1(wakeup, DS3231_A1_Minute);

    rtc_enable_wakeup_interrupt(s_rtcInterruptPin);
}

// -----------------------------------------------------------------------------
// RTC sanity / boot recovery
// -----------------------------------------------------------------------------

/**
 * @brief Basic RTC sanity check.
 *
 * Checks that:
 * - Year must be within [2020, 2099]
 * - RTC time must not be earlier than firmware build time
 *
 * @param now   Current RTC time.
 * @param build Firmware build timestamp.
 * @return true if RTC time is considered sane, false otherwise.
 */
static bool rtc_sanity_ok(const DateTime& now, const DateTime& build) {
    if (now.year() < 2020 || now.year() > 2099)
        {
            return false;
    }
    if (now < build)
        {
            return false;
    }
    return true;
}

/**
 * @brief Boot-time RTC recovery strategy (trust / wait for GUI / fallback).
 *
 * This function implements the following logic:
 * - If RTC time is sane at boot: accept it and (if connected) optionally wait for GUI
 * time.
 * - If RTC time is not sane at boot: accept it but optionally wait for GUI time.
 *   - If connected: mark time as unverified and set a waiting deadline (e.g., 20s).
 *  - If not connected: immediately fallback to firmware build time.
 * - If GUI time is received within the waiting deadline
 *  (via `rtc_apply_external_time()`): accept it and mark time as verified.
 * - If the waiting deadline expires without receiving GUI time: fallback to firmware
 * build time.
 * - If time is accepted from either RTC or GUI, it is applied to the DS3231 and marked
 * as verified.
 * - Logs all relevant events and decisions for debugging.
 * @param connected True if an external GUI/serial link is present at boot, false
 * otherwise.
 *
 * @return true if the RTC policy was executed (regardless of whether time is valid),
 * false on critical failure.
 *
 * @note The waiting deadline is currently set to 20 seconds.
 * @see rtc_apply_external_time()
 */
bool rtc_boot_recover() {
    // Get current RTC time and firmware build time
    const DateTime build(F(__DATE__), F(__TIME__));
    const DateTime now = rtc.now();

    // Log rtc time, build time and lost power status at boot for debugging
    LOG_DEBUG(
        "RTC boot check: rtc=%04d-%02d-%02d %02d:%02d:%02d, "
        "build=%04d-%02d-%02d %02d:%02d:%02d, lostPower=%s",
        now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second(), build.year(), build.month(), build.day(), build.hour(),
        build.minute(), build.second(), rtc.lostPower() ? "YES" : "NO");

    // check if rtc time is within 2020-2099 and not before build time
    const bool time_in_range        = rtc_sanity_ok(now, build);
    rtc_state_instance.time_checked = !rtc.lostPower() && time_in_range;

    // initialize the waiting deadline to 0 (not waiting) or now+20s (waiting for GUI
    // time)
    rtc_state_instance.time_in_connected_mode = 0;

    // If time is already verified at boot, we are done. If connected, we can wait for
    // GUI time if needed.
    if (rtc_state_instance.time_checked)
        {
            LOG_INFO("RTC time accepted as valid at boot: %04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(),
                     now.minute(), now.second());
    } else
        {
            // Else we fall back to firmware build time
            rtc.adjust(build);
            rtc_state_instance.time_checked = true;

            LOG_WARN(
                "RTC invalid at boot; using build time fallback: %04d-%02d-%02d "
                "%02d:%02d:%02d",
                build.year(), build.month(), build.day(), build.hour(), build.minute(), build.second());
        }
}

/**
 * @brief Apply time provided by an external GUI and finalize time verification.
 *
 * - Updates DS3231 time using `rtc.adjust(t)`.
 * - Marks time as verified and clears the waiting deadline.
 * - If no daily data file exists yet, creates it immediately.
 *
 * @param t External trusted time to apply.
 *
 * @note If logs were buffered while time was unverified, creating the daily file
 *       allows the system to flush buffered logs (depending on logging implementation).
 *
 * @see rtc_boot_recover()
 */
void rtc_apply_external_time(const DateTime& t) {
    rtc.adjust(t);
    rtc_state_instance.time_checked           = true;
    rtc_state_instance.time_in_connected_mode = 0;

    LOG_INFO("RTC updated from GUI (SET_CONFIG)");
}

/** @} */  // end of rtc_manager group
