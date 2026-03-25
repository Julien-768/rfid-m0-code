#include "ir_pwm.h"
#include "wiring_private.h"

/**
 * @brief Static instance used by ISR
 */
ir_pwm* ir_pwm::instance = nullptr;

/**
 * @brief Constructor
 */
ir_pwm::ir_pwm(uint8_t pwm_pin, uint8_t sensor1_pin, uint8_t sensor2_pin)
    : _pwm_pin(pwm_pin), _sensor1_pin(sensor1_pin), _sensor2_pin(sensor2_pin) {
    instance = this;
}

/**
 * @brief Initialize driver
 */
void ir_pwm::begin(bool enable_sensor_1, bool enable_sensor_2, ir_isr_callback_t callback) {
    _enable_sensor_1 = enable_sensor_1;
    _enable_sensor_2 = enable_sensor_2;
    _callback        = callback;

    pinMode(_pwm_pin, OUTPUT);
    digitalWrite(_pwm_pin, LOW);

    if (_enable_sensor_1) {
        pinMode(_sensor1_pin, INPUT);
    }

    if (_enable_sensor_2) {
        pinMode(_sensor2_pin, INPUT);
    }

    setup_pwm();
    setup_interrupts();

    if (_enable_sensor_1 || _enable_sensor_2) {
        start_pwm();
    }
}

/**
 * @brief Enable PWM carrier
 */
void ir_pwm::start_pwm() {
    TCC0->CC[3].reg = 666;
    while (TCC0->SYNCBUSY.bit.CC3);
}

/**
 * @brief Disable PWM carrier
 */
void ir_pwm::stop_pwm() {
    TCC0->CC[3].reg = 0;
    while (TCC0->SYNCBUSY.bit.CC3);
}

/**
 * @brief Configure hardware PWM (~36 kHz, 50%)
 *
 * NOTE:
 * - Uses TCC0
 * - Requires compatible pin (WO[3])
 */
void ir_pwm::setup_pwm() {
    pinPeripheral(_pwm_pin, PIO_TIMER);

    PM->APBCMASK.reg |= PM_APBCMASK_TCC0;

    GCLK->GENDIV.reg = GCLK_GENDIV_ID(4) | GCLK_GENDIV_DIV(1);
    while (GCLK->STATUS.bit.SYNCBUSY);

    GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(4) | GCLK_GENCTRL_SRC_DFLL48M | GCLK_GENCTRL_GENEN;
    while (GCLK->STATUS.bit.SYNCBUSY);

    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_TCC0_TCC1 | GCLK_CLKCTRL_GEN_GCLK4 | GCLK_CLKCTRL_CLKEN;
    while (GCLK->STATUS.bit.SYNCBUSY);

    TCC0->CTRLA.bit.SWRST = 1;
    while (TCC0->SYNCBUSY.bit.SWRST || TCC0->CTRLA.bit.SWRST);

    TCC0->WAVE.reg = TCC_WAVE_WAVEGEN_NPWM;
    while (TCC0->SYNCBUSY.bit.WAVE);

    // 48 MHz / (PER + 1) ≈ 36 kHz
    TCC0->PER.reg = 1332;
    while (TCC0->SYNCBUSY.bit.PER);

    // 50% duty cycle
    TCC0->CC[3].reg = 666;
    while (TCC0->SYNCBUSY.bit.CC3);

    TCC0->CTRLA.reg = TCC_CTRLA_PRESCALER_DIV1 | TCC_CTRLA_ENABLE;
    while (TCC0->SYNCBUSY.bit.ENABLE);
}

/**
 * @brief Configure GPIO interrupts
 */
void ir_pwm::setup_interrupts() {
    if (_enable_sensor_1) {
        attachInterrupt(digitalPinToInterrupt(_sensor1_pin), ir_pwm::isr_sensor1, CHANGE);
    }

    if (_enable_sensor_2) {
        attachInterrupt(digitalPinToInterrupt(_sensor2_pin), ir_pwm::isr_sensor2, CHANGE);
    }
}

/**
 * @brief ISR wrapper sensor 1
 */
void ir_pwm::isr_sensor1() {
    if (instance) {
        instance->handle_interrupt(instance->_sensor1_pin);
    }
}

/**
 * @brief ISR wrapper sensor 2
 */
void ir_pwm::isr_sensor2() {
    if (instance) {
        instance->handle_interrupt(instance->_sensor2_pin);
    }
}

/**
 * @brief Common ISR handler
 *
 * Reads pin state and forwards to user callback
 */
void ir_pwm::handle_interrupt(uint8_t pin) {
    if (_callback == nullptr) return;

    uint8_t state = digitalRead(pin);

    _callback(pin, state);
}
