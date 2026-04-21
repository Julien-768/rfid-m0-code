#pragma once

#include <stdint.h>
#include <ArduinoLowPower.h>

namespace pwr_manager {
void begin();
void update();

void request_shutdown();
bool power_button_read_state();

void enable_button_wakeup(void (*callback)());
void disable_button_wakeup();
bool handle_button_wakeup();

void ir_power_on();
void ir_power_off();
bool ir_is_on();

bool rfid_pwr_on(uint8_t rfid_mode);
void rfid_pwr_off(uint8_t rfid_mode);
bool rfid_is_on();
}  // namespace pwr_manager
