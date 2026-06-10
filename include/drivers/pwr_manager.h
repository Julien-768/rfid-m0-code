/**
 * @file pwr_manager.h
 * @brief Power management API for the device.
 *
 * This module provides:
 * - power button handling and event detection
 * - system shutdown control
 * - IR sensor power rail control
 * - RFID power rail control
 *
 * The implementation is platform-specific and currently targets the
 * Adafruit Feather M0 (ATSAMD21G18).
 */

#pragma once

#include <stdint.h>

namespace pwr_manager {

/**
 * @brief Power button event types.
 */
enum power_button_event_type_t : uint8_t {
    POWER_BUTTON_EVENT_NONE = 0,  ///< No event.
    POWER_BUTTON_SHORT_PRESS,     ///< Short button press detected.
    POWER_BUTTON_LONG_PRESS,      ///< Long button press detected.
};

/**
 * @brief Power button event information.
 *
 * Contains the event type and the measured button press duration.
 */
struct power_button_event_t {
    power_button_event_type_t type;  ///< Event type.
    uint32_t duration_ms;            ///< Press duration in milliseconds.
};

/**
 * @brief Reset internal power button tracking state.
 */
void reset_power_button_tracking();

/**
 * @brief Retrieve and consume the next power button event.
 *
 * @param event Output event structure.
 * @return true if an event was available.
 */
bool consume_power_button_event(power_button_event_t& event);

/**
 * @brief Initialize the power manager.
 */
void begin();

/**
 * @brief Periodic power manager update.
 */
void update();

/**
 * @brief Request system shutdown.
 */
void request_shutdown();

/**
 * @brief Read the current power button state.
 *
 * @return true if pressed.
 */
bool power_button_read_state();

/**
 * @brief Attach power button wakeup handling.
 *
 * @param callback Wakeup callback.
 */
void button_interrupt_attach(void (*callback)());

/**
 * @brief Detach power button wakeup handling.
 */
void button_interrupt_detach();

/**
 * @brief Power button interrupt handler.
 */
void power_button_irq_handler();

/**
 * @brief Poll and debounce the power button state.
 */
void power_button_poll();

/**
 * @brief Check whether a button event is pending.
 *
 * @return true if an event is available.
 */
bool power_button_event_pending();

/**
 * @brief Enable IR sensor power rail.
 */
void ir_power_on();

/**
 * @brief Disable IR sensor power rail.
 */
void ir_power_off();

/**
 * @brief Get IR power state.
 *
 * @return true if IR power is enabled.
 */
bool ir_is_on();

/**
 * @brief Enable RFID power rail.
 *
 * This function only enables the RFID 5V rail. UART preparation and reader
 * boot detection are handled by the RFID driver.
 *
 * @param rfid_mode RFID mode identifier. Currently unused.
 * @return true when the RFID power rail is enabled.
 */
bool rfid_pwr_on(uint8_t rfid_mode);

/**
 * @brief Disable RFID power rail.
 *
 * This function turns off the 5V rail used by the RFID reader.
 * UART ownership is handled separately by the RFID driver.
 *
 * @param rfid_mode RFID mode identifier. Currently unused.
 */
void rfid_pwr_off(uint8_t rfid_mode);

/**
 * @brief Get RFID power state.
 *
 * @return true if RFID power rail is enabled.
 */
bool rfid_is_on();

}  // namespace pwr_manager
