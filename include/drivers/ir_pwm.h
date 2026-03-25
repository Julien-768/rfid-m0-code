#pragma once
#include <Arduino.h>

/**
 * @brief ISR callback type
 *
 * @param pin   Pin that triggered interrupt
 * @param state Current pin state (HIGH / LOW)
 *
 * WARNING:
 * - Called inside ISR context
 * - Must be fast and non-blocking
 */
typedef void (*ir_isr_callback_t)(uint8_t pin, uint8_t state);

/**
 * @brief IR PWM + interrupt dispatcher driver
 *
 * Features:
 * - Hardware PWM generation (~36 kHz)
 * - GPIO interrupt handling
 * - ISR callback forwarding
 *
 * No application logic inside.
 */
class ir_pwm {
   public:
    ir_pwm(uint8_t pwm_pin, uint8_t sensor1_pin, uint8_t sensor2_pin);

    void begin(bool enable_sensor_1, bool enable_sensor_2, ir_isr_callback_t callback);

    void start_pwm();
    void stop_pwm();

   private:
    static ir_pwm* instance;

    uint8_t _pwm_pin;
    uint8_t _sensor1_pin;
    uint8_t _sensor2_pin;

    bool _enable_sensor_1 = false;
    bool _enable_sensor_2 = false;

    ir_isr_callback_t _callback = nullptr;

    void setup_pwm();
    void setup_interrupts();

    static void isr_sensor1();
    static void isr_sensor2();

    void handle_interrupt(uint8_t pin);
};
