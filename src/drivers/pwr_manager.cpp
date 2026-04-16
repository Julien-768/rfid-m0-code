#include "pwr_manager.h"

#include <Arduino.h>

#include "log.h"
#include "hardware.h"

// -----------------------------------------------------------------------------
// Configuration hardware
// -----------------------------------------------------------------------------

#ifndef PIN_PWR_3V
#error "PIN_PWR_3V must be defined in hardware.h"
#endif

#ifndef PIN_PWR_5V
#error "PIN_PWR_5V must be defined in hardware.h"
#endif

#ifndef PWR_3V_ACTIVE_HIGH
#error "PWR_3V_ACTIVE_HIGH must be defined in hardware.h"
#endif

#ifndef PWR_5V_ACTIVE_HIGH
#error "PWR_5V_ACTIVE_HIGH must be defined in hardware.h"
#endif

namespace {

bool g_ir_on   = false;
bool g_rfid_on = false;

inline void write_power_pin(uint8_t pin, bool active, bool active_high) {
    digitalWrite(pin, (active == active_high) ? HIGH : LOW);
}

}  // namespace

namespace pwr_manager {

void begin() {
    pinMode(PIN_PWR_3V, OUTPUT);
    pinMode(PIN_PWR_5V, OUTPUT);

    // Fail-safe au boot : tout éteint.
    write_power_pin(PIN_PWR_3V, false, PWR_3V_ACTIVE_HIGH);
    write_power_pin(PIN_PWR_5V, false, PWR_5V_ACTIVE_HIGH);

    g_ir_on   = false;
    g_rfid_on = false;

    LOG_INFO("Power manager initialized");
}

void ir_power_on() {
    if (g_ir_on) {
        return;
    }

    write_power_pin(PIN_PWR_3V, true, PWR_3V_ACTIVE_HIGH);
    delayMicroseconds(200);  // petit temps de stabilisation si besoin
    g_ir_on = true;

    LOG_DEBUG("IR power ON");
}

void ir_power_off() {
    if (!g_ir_on) {
        return;
    }

    write_power_pin(PIN_PWR_3V, false, PWR_3V_ACTIVE_HIGH);
    g_ir_on = false;

    LOG_DEBUG("IR power OFF");
}

bool ir_is_on() {
    return g_ir_on;
}

bool rfid_pwr_on(uint8_t rfid_mode) {
    (void)rfid_mode;

    if (g_rfid_on) {
        return true;
    }

    write_power_pin(PIN_PWR_5V, true, PWR_5V_ACTIVE_HIGH);

    // Laisse le module RFID se stabiliser.
    delay(10);

    g_rfid_on = true;

    LOG_DEBUG("RFID power ON");
    return true;
}

void rfid_pwr_off(uint8_t rfid_mode) {
    (void)rfid_mode;

    if (!g_rfid_on) {
        return;
    }

    write_power_pin(PIN_PWR_5V, false, PWR_5V_ACTIVE_HIGH);
    g_rfid_on = false;

    LOG_DEBUG("RFID power OFF");
}

bool rfid_is_on() {
    return g_rfid_on;
}

}  // namespace pwr_manager
