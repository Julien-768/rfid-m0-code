#include "pwr_manager.h"

#include <Arduino.h>

#include "log.h"
#include "hardware.h"
#include "wiring_private.h"

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

#ifndef PIN_PW_SW
#error "PIN_PW_SW must be defined in hardware.h"
#endif

#ifndef PIN_PW_EN
#error "PIN_PW_EN must be defined in hardware.h"
#endif

#ifndef PWR_EN_ACTIVE_HIGH
#error "PWR_EN_ACTIVE_HIGH must be defined in hardware.h"
#endif

#ifndef PW_SW_ACTIVE_HIGH
#define PW_SW_ACTIVE_HIGH true
#endif

#ifndef POWER_BUTTON_LONG_PRESS_MS
#define POWER_BUTTON_LONG_PRESS_MS 1200UL
#endif

#ifndef POWER_BUTTON_FEEDBACK_PERIOD_MS
#define POWER_BUTTON_FEEDBACK_PERIOD_MS 100UL
#endif

namespace {

bool g_ir_on   = false;
bool g_rfid_on = false;

bool g_pw_button_pressed         = false;
bool g_shutdown_requested        = false;
unsigned long g_pw_press_start   = 0;
unsigned long g_last_feedback_ms = 0;
bool g_feedback_on               = false;

inline void write_power_pin(uint8_t pin, bool active, bool active_high) {
    digitalWrite(pin, (active == active_high) ? HIGH : LOW);
    LOG_DEBUG("pin %u set to %s", pin, digitalRead(pin) ? "HIGH" : "LOW");
}

inline bool read_active_pin(uint8_t pin, bool active_high) {
    return digitalRead(pin) == (active_high ? HIGH : LOW);
}

/*
Switch power control logic:
- Power enable pin (PIN_PW_EN)
*/
inline void set_power_enabled(bool enabled) {
    write_power_pin(PIN_PW_EN, enabled, PWR_EN_ACTIVE_HIGH);
    LOG_DEBUG("pin %u set to %s (power %s)", PIN_PW_EN, digitalRead(PIN_PW_EN));
}

inline void set_feedback_output(bool on) {
    g_feedback_on = on;
    (void)on;
}

}  // namespace

// Hooks optionnels a definir ailleurs dans le projet si besoin.
void __attribute__((weak)) pwr_manager_on_short_press() {}
void __attribute__((weak)) pwr_manager_on_before_shutdown(const char* reason) {
    (void)reason;
}

namespace pwr_manager {

void begin() {
    pinMode(PIN_PWR_3V, OUTPUT);
    pinMode(PIN_PWR_5V, OUTPUT);
    pinMode(PIN_PW_SW, INPUT);
    pinMode(PIN_PW_EN, OUTPUT);

    // Fail-safe au boot : sous-modules OFF, maintien d'alimentation ON.
    write_power_pin(PIN_PWR_3V, false, PWR_3V_ACTIVE_HIGH);
    write_power_pin(PIN_PWR_5V, false, PWR_5V_ACTIVE_HIGH);
    set_power_enabled(true);

    g_ir_on              = false;
    g_rfid_on            = false;
    g_pw_button_pressed  = false;
    g_shutdown_requested = false;
    g_pw_press_start     = 0;
    g_last_feedback_ms   = 0;
    g_feedback_on        = false;

    LOG_INFO("Power manager initialized");
}

void enable_button_wakeup(void (*callback)()) {
    LowPower.attachInterruptWakeup(PIN_PW_SW, callback, PW_SW_ACTIVE_HIGH ? RISING : FALLING);
}

void disable_button_wakeup() {
    detachInterrupt(digitalPinToInterrupt(PIN_PW_SW));

    EExt_Interrupts in = g_APinDescription[PIN_PW_SW].ulExtInt;
    if (in != NOT_AN_INTERRUPT && in != EXTERNAL_INT_NMI) {
        EIC->WAKEUP.reg &= ~(1 << in);
    }
}

bool handle_button_wakeup() {
    if (g_shutdown_requested) {
        LOG_DEBUG("Wakeup event ignored: shutdown already requested");
        return false;
    }

    if (!power_button_read_state()) {
        LOG_DEBUG("Wakeup event ignored: power button not pressed");
        return false;
    }

    g_pw_button_pressed = true;
    g_pw_press_start    = millis();
    g_last_feedback_ms  = g_pw_press_start;

    while (power_button_read_state()) {
        const unsigned long now = millis();

        if ((now - g_last_feedback_ms) >= POWER_BUTTON_FEEDBACK_PERIOD_MS) {
            g_last_feedback_ms = now;
            set_feedback_output(!g_feedback_on);
        }

        if ((now - g_pw_press_start) >= POWER_BUTTON_LONG_PRESS_MS) {
            g_pw_button_pressed = false;
            set_feedback_output(false);
            return g_shutdown_requested;
        }

        delay(10);
    }

    const unsigned long press_duration = millis() - g_pw_press_start;
    g_pw_button_pressed                = false;
    set_feedback_output(false);

    if (press_duration < POWER_BUTTON_LONG_PRESS_MS) {
        LOG_INFO("Battery check by user");
        pwr_manager_on_short_press();
    }
    return false;
}

void request_shutdown() {
    if (g_shutdown_requested) {
        return;
    }

    g_shutdown_requested = true;
    set_feedback_output(true);

    delay(1000);
    set_power_enabled(false);

    // Attente de la coupure physique.
    while (true) {
        delay(1000);
    }
}

bool power_button_read_state() {
    return read_active_pin(PIN_PW_SW, PW_SW_ACTIVE_HIGH);
}

void update() {
    if (g_shutdown_requested) {
        return;
    }

    const bool pressed      = power_button_read_state();
    const unsigned long now = millis();

    if (pressed && !g_pw_button_pressed) {
        g_pw_button_pressed = true;
        g_pw_press_start    = now;
        g_last_feedback_ms  = now;
    }

    if (pressed && g_pw_button_pressed) {
        if ((now - g_last_feedback_ms) >= POWER_BUTTON_FEEDBACK_PERIOD_MS) {
            g_last_feedback_ms = now;
            set_feedback_output(!g_feedback_on);
        }

        if ((now - g_pw_press_start) >= POWER_BUTTON_LONG_PRESS_MS) {
            request_shutdown();
            return;
        }
    }

    if (!pressed && g_pw_button_pressed) {
        const unsigned long press_duration = now - g_pw_press_start;
        g_pw_button_pressed                = false;
        set_feedback_output(false);

        if (press_duration < POWER_BUTTON_LONG_PRESS_MS) {
            LOG_INFO("Battery check by user");
            pwr_manager_on_short_press();
        }
    }
}

void ir_power_on() {
    if (g_ir_on) {
        return;
    }

    write_power_pin(PIN_PWR_3V, true, PWR_3V_ACTIVE_HIGH);
    delayMicroseconds(200);
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
