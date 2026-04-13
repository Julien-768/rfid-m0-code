#include "ir_pwm.h"
#include "wiring_private.h"
#include "ArduinoLowPower.h"
#include "log.h"

/**
 * @brief Static instance used by ISR wrappers.
 */
ir_pwm* ir_pwm::instance = nullptr;

/**
 * @brief Constructor.
 *
 * @param pwm_pin GPIO pin for PWM output.
 * @param sensor1_pin GPIO pin for sensor 1 input.
 * @param sensor2_pin GPIO pin for sensor 2 input.
 */
ir_pwm::ir_pwm(uint8_t pwm_pin, uint8_t sensor1_pin, uint8_t sensor2_pin)
    : _pwm_pin(pwm_pin), _sensor1_pin(sensor1_pin), _sensor2_pin(sensor2_pin) {
    instance = this;
}

/**
 * @brief Initialize the driver.
 *
 * @param enable_sensor_1 Enable interrupt handling for sensor 1.
 * @param enable_sensor_2 Enable interrupt handling for sensor 2.
 * @param callback_sensor_1 Callback for sensor 1.
 * @param callback_sensor_2 Callback for sensor 2.
 */
void ir_pwm::begin(bool enable_sensor_1, bool enable_sensor_2, ir_isr_callback_t callback_sensor_1,
                   ir_isr_callback_t callback_sensor_2) {

    LOG_DEBUG("ir_pwm::begin - Pin Output pwm=%d Pin Input PR1=%d Pin Input PR2=%d", _pwm_pin,
              _sensor1_pin, _sensor2_pin);

    _enable_sensor_1 = enable_sensor_1;
    _enable_sensor_2 = enable_sensor_2;

    _callback_sensor_1 = callback_sensor_1;
    _callback_sensor_2 = callback_sensor_2;

    pinMode(_pwm_pin, OUTPUT);
    digitalWrite(_pwm_pin, LOW);

    if (_enable_sensor_1) {
        LOG_DEBUG("Sensor 1 enabled on pin %d", _sensor1_pin);
        pinMode(_sensor1_pin, INPUT);
    }

    if (_enable_sensor_2) {
        LOG_DEBUG("Sensor 2 enabled on pin %d", _sensor2_pin);
        pinMode(_sensor2_pin, INPUT);
    }

    LOG_DEBUG("Setting up PWM and interrupts");
    setup_pwm();
    setup_interrupts();

    if (_enable_sensor_1 || _enable_sensor_2) {
        LOG_DEBUG("Starting PWM carrier");
        start_pwm();
    }
}

/**
 * @brief Enable PWM carrier.
 */
void ir_pwm::start_pwm() {
    LOG_DEBUG("PWM start");

    // Before modifying the CC register, ensure any previous update has completed by checking SYNCBUSY.
    while (TCC0->SYNCBUSY.reg) {}

    TCC0->CC[3].reg = 666;

    uint32_t t0 = millis();
    while (TCC0->SYNCBUSY.bit.CC3) {
        if (millis() - t0 > 100) {
            LOG_ERROR("start_pwm timeout on CC3 sync");
            break;
        }
    }

    LOG_DEBUG("PWM carrier started");
}

/**
 * @brief Disable PWM carrier.
 */
void ir_pwm::stop_pwm() {
    LOG_DEBUG("PWM stop");

    // Before modifying the CC register, ensure any previous update has completed by checking SYNCBUSY.
    while (TCC0->SYNCBUSY.reg) {}

    TCC0->CC[3].reg = 0;

    uint32_t timeout = millis();
    while (TCC0->SYNCBUSY.bit.CC3) {
        if (millis() - timeout > 100) {
            LOG_ERROR("Timeout waiting for TCC0 CC3 sync in stop_pwm");
            break;
        }
    }
}

/**
 * @brief Configure hardware PWM (~36 kHz, 50% duty cycle).
 *
 * @note This implementation uses TCC0 WO[3].
 *       The PWM pin must match this hardware mapping.
 */
void ir_pwm::setup_pwm() {
    LOG_DEBUG("Configuring PWM (TCC0)");

    pinPeripheral(_pwm_pin, PIO_TIMER);

    PM->APBCMASK.reg |= PM_APBCMASK_TCC0;

    GCLK->GENDIV.reg = GCLK_GENDIV_ID(4) | GCLK_GENDIV_DIV(1);
    while (GCLK->STATUS.bit.SYNCBUSY) {}

    GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(4) | GCLK_GENCTRL_SRC_DFLL48M | GCLK_GENCTRL_GENEN;
    while (GCLK->STATUS.bit.SYNCBUSY) {}

    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_TCC0_TCC1 | GCLK_CLKCTRL_GEN_GCLK4 | GCLK_CLKCTRL_CLKEN;
    while (GCLK->STATUS.bit.SYNCBUSY) {}

    TCC0->CTRLA.bit.SWRST = 1;
    while (TCC0->SYNCBUSY.bit.SWRST || TCC0->CTRLA.bit.SWRST) {}

    TCC0->WAVE.reg = TCC_WAVE_WAVEGEN_NPWM;
    while (TCC0->SYNCBUSY.bit.WAVE) {}

    /* 48 MHz / (1332 + 1) ~= 36 kHz */
    TCC0->PER.reg = 1332;
    while (TCC0->SYNCBUSY.bit.PER) {}

    /* 50 % duty cycle */
    TCC0->CC[3].reg = 666;
    while (TCC0->SYNCBUSY.bit.CC3) {}

    TCC0->CTRLA.reg = TCC_CTRLA_PRESCALER_DIV1 | TCC_CTRLA_ENABLE;
    while (TCC0->SYNCBUSY.bit.ENABLE) {}

    LOG_DEBUG("PWM configured: freq≈36kHz duty=50%%");
}

/**
 * @brief Configure GPIO interrupts for enabled sensors.
 *
 * This function uses ArduinoLowPower so the sensor pins can also
 * act as wake-up sources during sleep.
 */
void ir_pwm::setup_interrupts() {
    if (_enable_sensor_1) {
        LOG_DEBUG("Attach interrupt sensor 1");
        LowPower.attachInterruptWakeup(_sensor1_pin, ir_pwm::isr_sensor_1, CHANGE);
    }

    if (_enable_sensor_2) {
        LOG_DEBUG("Attach interrupt sensor 2");
        LowPower.attachInterruptWakeup(_sensor2_pin, ir_pwm::isr_sensor_2, CHANGE);
    }
}

/**
 * @brief Static ISR wrapper for sensor 1.
 */
void ir_pwm::isr_sensor_1() {
    if (instance != nullptr) {
        instance->handle_interrupt_sensor_1();
    }
}

/**
 * @brief Static ISR wrapper for sensor 2.
 */
void ir_pwm::isr_sensor_2() {
    if (instance != nullptr) {
        instance->handle_interrupt_sensor_2();
    }
}

/**
 * @brief Handle sensor 1 interrupt.
 *
 * Reads the current pin state and forwards it to the registered
 * callback for sensor 1.
 */
void ir_pwm::handle_interrupt_sensor_1() {
    if (_callback_sensor_1 == nullptr) {
        return;
    }

    uint8_t state = digitalRead(_sensor1_pin);

    // ⚠️ Avoid heavy logging in ISR
    // LOG_DEBUG("ISR S1 state=%d", state);

    _callback_sensor_1(state);
}

/**
 * @brief Handle sensor 2 interrupt.
 *
 * Reads the current pin state and forwards it to the registered
 * callback for sensor 2.
 */
void ir_pwm::handle_interrupt_sensor_2() {
    if (_callback_sensor_2 == nullptr) {
        return;
    }

    uint8_t state = digitalRead(_sensor2_pin);

    // ⚠️ Avoid heavy logging in ISR
    // LOG_DEBUG("ISR S2 state=%d", state);

    _callback_sensor_2(state);
}

/**
 * @brief Update the callback used for sensor 1.
 *
 * @param callback New callback for sensor 1.
 */
void ir_pwm::set_callback_sensor_1(ir_isr_callback_t callback) {
    LOG_DEBUG("Set ir_pwm callback sensor 1");
    _callback_sensor_1 = callback;
}

/**
 * @brief Update the callback used for sensor 2.
 *
 * @param callback New callback for sensor 2.
 */
void ir_pwm::set_callback_sensor_2(ir_isr_callback_t callback) {
    LOG_DEBUG("Set ir_pwm callback sensor 2");
    _callback_sensor_2 = callback;
}
