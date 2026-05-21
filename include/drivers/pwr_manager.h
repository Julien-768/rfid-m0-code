#pragma once

#include <stdint.h>
#include <ArduinoLowPower.h>
namespace pwr_manager {

enum power_button_event_type_t : uint8_t {
    POWER_BUTTON_EVENT_NONE = 0,
    POWER_BUTTON_SHORT_PRESS,
    POWER_BUTTON_LONG_PRESS,
};

struct power_button_event_t {
    power_button_event_type_t type;
    uint32_t duration_ms;
};

void reset_power_button_tracking();
bool consume_power_button_event(power_button_event_t& event);

void begin();
void update();

void request_shutdown();
bool power_button_read_state();

void button_interrupt_attach(void (*callback)());
void button_interrupt_detach();
void power_button_irq_handler();

void power_button_poll();
bool power_button_event_pending();

void ir_power_on();
void ir_power_off();
bool ir_is_on();

bool rfid_pwr_on(uint8_t rfid_mode);

/**
 * @brief Disable RFID power and release RFID UART ownership.
 *
 * @param rfid_mode RFID mode identifier.
 */
void rfid_pwr_off(uint8_t rfid_mode);

bool rfid_is_on();
}  // namespace pwr_manager
