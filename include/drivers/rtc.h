#pragma once

#include <Arduino.h>
#include <RTClib.h>

/**
 * @file rtc.h
 * @defgroup RTC_Manager RTC Manager
 * @ingroup SystemModules
 * @brief RTC DS3231 management (time, alarms, wake scheduling) for Moonraker.
 *
 * This module exposes:
 * - RTC initialization and presence check (I2C)
 * - Time sanity checks at boot (build time vs RTC time)
 * - Alarm scheduling for periodic wake-ups
 * - RTC wake-up interrupt configuration
 * - Optional user callback executed from the RTC ISR
 *
 * @note `Wire.begin()` must be called before using this module (e.g. in `setup()`).
 * @note The global RTC instance is provided as `rtc` for convenience.
 * @{
 */

/**
 * @brief Compact BCD date/time structure used by Moonraker (years since 2000).
 *
 * Each field is stored in BCD format:
 * - year: 00..99 (for 2000..2099)
 * - month/day/hour/minute/second: standard ranges
 */
typedef struct {
    uint8_t year;    ///< 00..99 (BCD, years since 2000)
    uint8_t month;   ///< 01..12 (BCD)
    uint8_t day;     ///< 01..31 (BCD)
    uint8_t hour;    ///< 00..23 (BCD)
    uint8_t minute;  ///< 00..59 (BCD)
    uint8_t second;  ///< 00..59 (BCD)
} LoggerTime_t;

/**
 * @brief Runtime RTC state used by the application.
 */
struct RTC_STATE {
    bool time_checked               = true;  ///< True if RTC time is considered verified.
    uint32_t time_in_connected_mode = 0;     ///< Optional deadline used by the application.
    uint8_t last_log_day            = 0;     ///< Day-of-month of the last created daily log file.
};

/**
 * @brief Callback type executed when the RTC alarm interrupt fires.
 *
 * @note This callback runs in interrupt context.
 *       Keep it short and non-blocking.
 */
typedef void (*rtc_alarm_callback_t)();

/**
 * @brief Returns a reference to the global RTC object (RTClib).
 *
 * @return Reference to the global RTC instance.
 */
RTC_DS3231& get_rtc();

/**
 * @brief Access global RTC instance.
 */
inline RTC_DS3231& rtc() {
    return get_rtc();
}

/**
 * @brief Returns a reference to the global RTC state structure.
 *
 * @return Reference to the global RTC runtime state.
 */
RTC_STATE& get_rtc_state();

/**
 * @brief Access global RTC state.
 */
inline RTC_STATE& rtc_state() {
    return get_rtc_state();
}

/**
 * @name Alarm identifiers
 * @brief Alarm indices used by RTClib's `clearAlarm()` / `disableAlarm()` APIs.
 *
 * @note We intentionally avoid names like `DS3231_ALARM_1` from RTClib internals
 *       to keep a stable local API.
 * @{
 */
constexpr uint8_t DS3231_ALARM_1 = 1;  ///< Alarm 1 index (used for periodic wake-up)
constexpr uint8_t DS3231_ALARM_2 = 2;  ///< Alarm 2 index (optional / currently unused)
/** @} */

// -----------------------------------------------------------------------------
// Init / status
// -----------------------------------------------------------------------------

/**
 * @brief Initialize the DS3231 RTC and configure the RTC wake-up interrupt.
 *
 * - Checks that the I2C bus is ready
 * - Initializes the RTC
 * - Clears alarm flags
 * - Attaches the internal RTC ISR on the given interrupt pin
 *
 * @param interrupt_pin Arduino pin connected to DS3231 INT/SQW.
 * @return true if the RTC responded and was configured successfully, false otherwise.
 */
bool rtc_initialization(uint32_t interrupt_pin);

/**
 * @brief Quick I2C probe to check if DS3231 (0x68) is present.
 *
 * @return true if device ACKs on address 0x68, false otherwise.
 */
bool scan_i2c_for_ds3231();

/**
 * @brief Clear the internal software alarm flag and clear RTC Alarm1 state.
 */
void rtc_clear_alarm_flag();

/**
 * @brief Return true if the RTC alarm fired since the last clear.
 *
 * @return true if an RTC alarm interrupt occurred, false otherwise.
 */
bool rtc_alarm_fired();

// -----------------------------------------------------------------------------
// Wake-up scheduling
// -----------------------------------------------------------------------------

/**
 * @brief Configure the RTC interrupt pin as a wake-up source.
 *
 * This attaches the provided ISR to the DS3231 INT/SQW pin and enables wake-up
 * from low-power sleep.
 *
 * @param interrupt_pin Arduino pin connected to DS3231 INT/SQW.
 * @param isr Interrupt service routine attached to this pin.
 */
void rtc_configure_interrupt(uint8_t interrupt_pin, void (*isr)());

/**
 * @brief Schedule the next periodic wake-up using RTC Alarm1.
 *
 * @param now Current time.
 * @param interval_s Wake interval in seconds.
 */
void rtc_clear_and_set_alarm(const DateTime& now, uint16_t interval_s);

/**
 * @brief Set or replace the optional user callback executed by the RTC ISR.
 *
 * The internal RTC ISR always sets the module alarm flag. If a user callback is
 * registered, it is also executed from interrupt context.
 *
 * @param callback Callback to execute when the RTC alarm fires.
 *                 Pass nullptr to disable the callback.
 */
void rtc_set_alarm_callback(rtc_alarm_callback_t callback);

// -----------------------------------------------------------------------------
// Boot / GUI time handling
// -----------------------------------------------------------------------------

/**
 * @brief Boot-time RTC sanity + recovery policy.
 *
 * - If RTC time is sane: accept it.
 * - If not sane: fallback to firmware build time.
 *
 * @return true if RTC policy executed successfully.
 */
bool rtc_boot_recover();

/**
 * @brief Apply time received from the GUI/tool and mark RTC time as verified.
 *
 * @param t Time to write into RTC.
 */
void rtc_apply_external_time(const DateTime& t);

/** @} */  // end of RTC_Manager
