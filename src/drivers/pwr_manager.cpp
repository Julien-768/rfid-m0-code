#include "pwr_manager.h"

#include <Arduino.h>

#include "log.h"
#include "hardware.h"
#include "wiring_private.h"
#include "irq_helper.h"

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
#define POWER_BUTTON_LONG_PRESS_MS 12000UL
#endif

namespace {

constexpr uint32_t POWER_BUTTON_DEBOUNCE_MS = 30;

bool g_button_is_pressed         = false;
bool g_button_last_raw           = false;
uint32_t g_button_last_change_ms = 0;
uint32_t g_button_press_ms       = 0;
uint32_t g_button_duration_ms    = 0;
bool g_button_duration_pending   = false;

bool g_ir_on   = false;
bool g_rfid_on = false;

bool g_shutdown_requested = false;

// volatile bool g_button_irq_is_pressed       = false;
// volatile uint32_t g_button_irq_press_ms     = 0;
// volatile uint32_t g_button_irq_duration_ms  = 0;
// volatile bool g_button_irq_duration_pending = false;

inline void write_power_pin(uint8_t pin, bool active, bool active_high) {
    digitalWrite(pin, (active == active_high) ? HIGH : LOW);
    LOG_DEBUG("pin %u set to %s", pin, digitalRead(pin) ? "HIGH" : "LOW");
}

inline bool read_active_pin(uint8_t pin, bool active_high) {
    return digitalRead(pin) == (active_high ? HIGH : LOW);
}

}  // namespace

namespace pwr_manager {

void reset_power_button_tracking() {
    // noInterrupts();
    // g_button_irq_is_pressed = false;
    // g_button_irq_press_ms   = 0;

    // g_button_irq_duration_ms      = 0;
    // g_button_irq_duration_pending = false;
    // interrupts();

    g_button_is_pressed       = false;
    g_button_last_raw         = power_button_read_state();
    g_button_last_change_ms   = millis();
    g_button_press_ms         = 0;
    g_button_duration_ms      = 0;
    g_button_duration_pending = false;
}

void begin() {
    pinMode(PIN_PWR_3V, OUTPUT);
    pinMode(PIN_PWR_5V, OUTPUT);
    pinMode(PIN_PW_SW, INPUT);
    pinMode(PIN_PW_EN, OUTPUT);

    // Fail-safe au boot : sous-modules OFF, maintien d'alimentation ON.
    write_power_pin(PIN_PWR_3V, false, PWR_3V_ACTIVE_HIGH);
    write_power_pin(PIN_PWR_5V, false, PWR_5V_ACTIVE_HIGH);
    write_power_pin(PIN_PW_EN, true, PWR_EN_ACTIVE_HIGH);
    LOG_DEBUG("Power hold relay ON on PIN_PW_EN");

    g_ir_on              = false;
    g_rfid_on            = false;
    g_shutdown_requested = false;
    reset_power_button_tracking();

    LOG_INFO("Power manager initialized");
}

void button_interrupt_attach(void (*callback)()) {
    // LowPower.attachInterruptWakeup(PIN_PW_SW, callback, CHANGE);
    // LOG_DEBUG("Button interrupt pin=%d digitalPinToInterrupt=%d extint=%d",
    //           PIN_PW_SW,
    //           digitalPinToInterrupt(PIN_PW_SW),
    //           g_APinDescription[PIN_PW_SW].ulExtInt);
    (void)callback;
    LOG_DEBUG("Power button uses polling, interrupt not attached");
}

void button_interrupt_detach() {
    // low_power_detach_interrupt(PIN_PW_SW);
}

bool power_button_read_state() {
    return read_active_pin(PIN_PW_SW, PW_SW_ACTIVE_HIGH);
}

void power_button_irq_handler() {
    // const uint32_t now    = millis();
    // const bool is_pressed = power_button_read_state();

    // if (is_pressed) {
    //     if (!g_button_irq_is_pressed) {
    //         g_button_irq_is_pressed = true;
    //         g_button_irq_press_ms   = now;
    //     }
    // } else {
    //     if (g_button_irq_is_pressed) {
    //         g_button_irq_is_pressed = false;

    //         g_button_irq_duration_ms      = now - g_button_irq_press_ms;
    //         g_button_irq_duration_pending = true;
    //     }
    // }
    power_button_poll();
}

bool consume_power_button_event(power_button_event_t& event) {
    power_button_poll();

    event.type        = POWER_BUTTON_EVENT_NONE;
    event.duration_ms = 0;

    const bool pressed = power_button_read_state();

    if (g_button_duration_pending) {
        const uint32_t duration_ms = g_button_duration_ms;

        event.duration_ms = duration_ms;
        event.type        = (duration_ms >= POWER_BUTTON_LONG_PRESS_MS) ? POWER_BUTTON_LONG_PRESS
                                                                        : POWER_BUTTON_SHORT_PRESS;

        reset_power_button_tracking();
        digitalWrite(PIN_BUZZER_LED, LOW);
        return true;
    }

    if (pressed && g_button_press_ms != 0) {
        const uint32_t press_duration = millis() - g_button_press_ms;

        digitalWrite(PIN_BUZZER_LED, HIGH);

        if (press_duration >= POWER_BUTTON_LONG_PRESS_MS) {
            event.type        = POWER_BUTTON_LONG_PRESS;
            event.duration_ms = press_duration;

            reset_power_button_tracking();
            digitalWrite(PIN_BUZZER_LED, LOW);
            return true;
        }

        return false;
    }

    digitalWrite(PIN_BUZZER_LED, LOW);
    return false;
}

void request_shutdown() {
    if (g_shutdown_requested) {
        return;
    }

    g_shutdown_requested = true;

    delay(1000);
    write_power_pin(PIN_PW_EN, false, PWR_EN_ACTIVE_HIGH);

    while (true) {
        delay(1000);
    }
}

void update() {
    if (g_shutdown_requested) {
        return;
    }

    power_button_event_t event = {};
    if (consume_power_button_event(event) && event.type == POWER_BUTTON_LONG_PRESS) {
        request_shutdown();
    }
}

void power_button_poll() {
    const uint32_t now = millis();
    const bool raw     = power_button_read_state();

    // LOG_DEBUG("Power button raw state: %d", raw);

    // State change detected, reset debounce timer.
    if (raw != g_button_last_raw) {
        g_button_last_raw       = raw;
        g_button_last_change_ms = now;
        return;
    }

    // If we're within the debounce period, ignore.
    if ((now - g_button_last_change_ms) < POWER_BUTTON_DEBOUNCE_MS) {
        return;
    }

    // If button is currently pressed and wasn't previously registered as pressed, mark as pressed and record press time.
    if (raw && !g_button_is_pressed) {
        LOG_DEBUG("Power button pressed");
        g_button_is_pressed = true;
        g_button_press_ms   = now;
    } else
        // If button is currently released and wasn't previously registered as released
        if (!raw && g_button_is_pressed) {
            LOG_DEBUG("Power button released after %lu ms", now - g_button_press_ms);
            g_button_is_pressed       = false;
            g_button_duration_ms      = now - g_button_press_ms;
            g_button_duration_pending = true;
        }
}

bool power_button_event_pending() {
    power_button_poll();
    return g_button_duration_pending;
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

    LOG_DEBUG("RFID 5V enabled, waiting reader startup");
    delay(500);

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
