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
 * - Daily file rollover helper
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
    bool time_checked = true;             ///< True if RTC time is considered invalid at boot
                                          ///< (waiting for GUI time).
    uint32_t time_in_connected_mode = 0;  ///< Deadline (millis) after which we fallback
                                          ///< to build time if still unverified.
    uint8_t last_log_day = 0;             ///< Day-of-month of the last created daily log file.
};

/**
 * @brief Returns a reference to the global RTC object (RTClib).
 */
extern RTC_DS3231& get_rtc();
// Macro for easier access
#define rtc get_rtc()

/**
 * @brief Returns a reference to the global RTC state structure.
 */
extern RTC_STATE& get_rtc_state();
// Macro for easier access
#define rtc_state get_rtc_state()

/**
 * @name Alarm identifiers
 * @brief Alarm indices used by RTClib's `clearAlarm()` / `disableAlarm()` APIs.
 *
 * @note We intentionally avoid names like `DS3231_ALARM_1` to prevent conflicts
 *       with RTClib enum symbols (`DS3231_A1_*`, etc.).
 * @{
 */
constexpr uint8_t DS3231_ALARM_1 = 1;  ///< Alarm 1 index (used for periodic wake-up)
constexpr uint8_t DS3231_ALARM_2 = 2;  ///< Alarm 2 index (optional / currently unused)
/** @} */

// -----------------------------------------------------------------------------
// Init / status
// -----------------------------------------------------------------------------

/**
 * @brief Initialize the RTC interrupt pin.
 * @param interruptPin The pin number to use for the RTC interrupt.
 */
void rtc_init_interrupt_pin(uint8_t interruptPin);

/**
 * @brief Initialize the DS3231 RTC and clear alarms.
 * @return true if the RTC responded and was configured, false otherwise.
 */
bool rtc_initialization();

/**
 * @brief Quick I2C probe to check if DS3231 (0x68) is present.
 * @return true if device ACKs on address 0x68.
 */
bool scan_i2c_for_ds3231();

/**
 * @brief Indicates if the last scheduled alarm triggered since last clear.
 * @return true if alarm flag is set.
 */
bool rtc_alarm_triggered();

/**
 * @brief Clear the internal alarm flag and clear RTC Alarm1 state.
 */
void rtc_clear_alarm_flag();

// -----------------------------------------------------------------------------
// Wake-up scheduling
// -----------------------------------------------------------------------------

/**
 * @brief Enable wake-up interrupt from the DS3231 INT/SQW pin.
 *
 * This attaches the pin interrupt to wake the MCU from LowPower sleep.
 */
void rtc_configure_interrupt(u_int32_t interrupt_pin, void (*isr)());

/**
 * @brief Schedule the next periodic wake-up using RTC Alarm1.
 *
 * @param now Current time.
 * @param interval_s Wake interval in seconds.
 */
void rtc_schedule_next_wake(const DateTime& now, uint16_t interval_s, u_int32_t interrupt_pin,
                            void (*isr)());

// -----------------------------------------------------------------------------
// Boot / GUI time handling
// -----------------------------------------------------------------------------

/**
 * @brief Boot-time RTC sanity + recovery policy.
 *
 * - If RTC time is sane: accept it.
 * - If not sane:
 *   - If connected: mark unverified and wait for GUI time.
 *   - If not connected: fallback to build time.
 *
 * @return true if RTC policy executed (does not mean "RTC time was valid").
 */
bool rtc_boot_recover();

/**
 * @brief Apply time received from the GUI/tool and mark RTC time as verified.
 * @param t Time to write into RTC.
 */
void rtc_apply_external_time(const DateTime& t);

/** @} */  // end of RTC_Manager
