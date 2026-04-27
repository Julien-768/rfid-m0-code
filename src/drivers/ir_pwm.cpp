#include "ir_pwm.h"

#include "SAMD21turboPWM.h"
#include "ArduinoLowPower.h"
#include "log.h"
#include "wiring_private.h"

/**
 * Platform: Adafruit Feather M0 (ATSAMD21G18)
 * MCU: ARM Cortex-M0+ @ 48 MHz
 * Framework: Arduino (SAMD core)
 * Logic level: 3.3V
 */

/**
 * @brief Static instance used by ISR wrappers.
 */
ir_pwm* ir_pwm::instance = nullptr;

/**
 * @brief Shared TurboPWM instance.
 *
 * PWM hardware is global on SAMD21, so this object is shared.
 */
static TurboPWM pwm;

/**
 * @brief Constructor.
 */
ir_pwm::ir_pwm(uint8_t pwm_pin, uint8_t sensor1_pin, uint8_t sensor2_pin)
    : _pwm_pin(pwm_pin), _sensor1_pin(sensor1_pin), _sensor2_pin(sensor2_pin) {
    instance = this;
}

/**
 * @brief Initialize the driver.
 */
void ir_pwm::begin(bool enable_sensor_1, bool enable_sensor_2, ir_isr_callback_t callback_sensor_1,
                   ir_isr_callback_t callback_sensor_2) {
    LOG_DEBUG("IR_PWM initialization: PWM pin=%d", _pwm_pin);
    LOG_DEBUG("Sensor 1: pin=%d, external interrupt=%d", _sensor1_pin,
              g_APinDescription[_sensor1_pin].ulExtInt);
    LOG_DEBUG("Sensor 2: pin=%d, external interrupt=%d", _sensor2_pin,
              g_APinDescription[_sensor2_pin].ulExtInt);

    _enable_sensor_1 = enable_sensor_1;
    _enable_sensor_2 = enable_sensor_2;

    _callback_sensor_1 = callback_sensor_1;
    _callback_sensor_2 = callback_sensor_2;

    pinMode(_pwm_pin, OUTPUT);
    digitalWrite(_pwm_pin, LOW);

    if (_enable_sensor_1) {
        pinMode(_sensor1_pin, INPUT_PULLUP);
    }

    if (_enable_sensor_2) {
        pinMode(_sensor2_pin, INPUT_PULLUP);
    }

    setup_pwm();
    enable_sensor_wakeups();

    _pwm_running = false;
}

/**
 * @brief Enable the PWM carrier with ~50% duty cycle.
 */
void ir_pwm::start_pwm() {
    if (_pwm_running) {
        return;
    }

    LOG_DEBUG("PWM start");
    pwm.analogWrite(_pwm_pin, PWM_DUTY_ON);
    _pwm_running = true;
}

/**
 * @brief Disable the PWM carrier.
 */
void ir_pwm::stop_pwm() {
    if (!_pwm_running) {
        return;
    }

    LOG_DEBUG("PWM stop");
    pwm.analogWrite(_pwm_pin, PWM_DUTY_OFF);
    _pwm_running = false;
}

/**
 * @brief Configure hardware PWM for an IR carrier (~36 kHz).
 *
 * The ocrdu TurboPWM library uses:
 * - setClockDivider(divider, turbo)
 * - timer(timer, prescaler, steps, fastPWM)
 *
 * With:
 *   input clock divider = 1
 *   timer prescaler     = 1
 *   timer steps         = 1333
 *
 * this gives approximately 36 kHz on a 48 MHz SAMD21 clock.
 *
 * The PWM output is configured OFF initially.
 */
void ir_pwm::setup_pwm() {
    LOG_DEBUG("Configuring TurboPWM (ocrdu)");

    pwm.setClockDivider(1, false);
    pwm.timer(PWM_TIMER, PWM_PRESCALER, PWM_STEPS, true);
    pwm.analogWrite(_pwm_pin, PWM_DUTY_OFF);

    LOG_DEBUG("PWM configured: ~36 kHz, ~50%% duty when enabled");
}

/**
 * @brief Configure interrupts for sensors.
 */
void ir_pwm::enable_sensor_wakeups() {
    if (_enable_sensor_1) {
        LowPower.attachInterruptWakeup(_sensor1_pin, ir_pwm::isr_sensor_1, CHANGE);
    }

    if (_enable_sensor_2) {
        LowPower.attachInterruptWakeup(_sensor2_pin, ir_pwm::isr_sensor_2, CHANGE);
    }
}

/**
 * @brief Disable interrupts for sensors.
 *
 * Must be called before stopping the PWM carrier to avoid spurious wake-ups.
 */
void ir_pwm::disable_sensor_wakeups() {
    if (_enable_sensor_1) {
        detachInterrupt(digitalPinToInterrupt(_sensor1_pin));

        EExt_Interrupts in1 = g_APinDescription[_sensor1_pin].ulExtInt;
        if (in1 != NOT_AN_INTERRUPT && in1 != EXTERNAL_INT_NMI) {
            EIC->WAKEUP.reg &= ~(1 << in1);
        }
    }

    if (_enable_sensor_2) {
        detachInterrupt(digitalPinToInterrupt(_sensor2_pin));

        EExt_Interrupts in2 = g_APinDescription[_sensor2_pin].ulExtInt;
        if (in2 != NOT_AN_INTERRUPT && in2 != EXTERNAL_INT_NMI) {
            EIC->WAKEUP.reg &= ~(1 << in2);
        }
    }
}

/**
 * @brief ISR wrapper for sensor 1.
 */
void ir_pwm::isr_sensor_1() {
    if (instance != nullptr) {
        instance->handle_interrupt_sensor_1();
    }
}

/**
 * @brief ISR wrapper for sensor 2.
 */
void ir_pwm::isr_sensor_2() {
    if (instance != nullptr) {
        instance->handle_interrupt_sensor_2();
    }
}

/**
 * @brief Handle sensor 1 interrupt.
 */
void ir_pwm::handle_interrupt_sensor_1() {
    if (_callback_sensor_1 == nullptr) {
        return;
    }

    uint8_t state = digitalRead(_sensor1_pin);
    _callback_sensor_1(state);
}

/**
 * @brief Handle sensor 2 interrupt.
 */
void ir_pwm::handle_interrupt_sensor_2() {
    if (_callback_sensor_2 == nullptr) {
        return;
    }

    uint8_t state = digitalRead(_sensor2_pin);
    _callback_sensor_2(state);
}

void ir_pwm::set_callback_sensor_1(ir_isr_callback_t callback) {
    _callback_sensor_1 = callback;
}

void ir_pwm::set_callback_sensor_2(ir_isr_callback_t callback) {
    _callback_sensor_2 = callback;
}
