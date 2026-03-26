#pragma once

#include <Arduino.h>

/**
 * @brief Callback type used for IR sensor interrupts.
 *
 * @param state Current digital state read on the sensor pin.
 *
 * @note This callback is executed in interrupt context.
 *       Keep it short and non-blocking.
 */
typedef void (*ir_isr_callback_t)(uint8_t state);

/**
 * @brief IR PWM driver for Feather M0 / SAMD21.
 *
 * This driver:
 * - generates a hardware PWM carrier for IR emission
 * - optionally enables interrupt wake-up on two sensor pins
 * - dispatches sensor interrupts to two independent callbacks
 *
 * Sensor callbacks are optional and can be updated later.
 */
class ir_pwm {
   public:
    /**
     * @brief Construct a new ir_pwm object.
     *
     * @param pwm_pin GPIO pin used for PWM output.
     * @param sensor1_pin GPIO pin used for sensor 1 input.
     * @param sensor2_pin GPIO pin used for sensor 2 input.
     *
     * @note The PWM pin must be compatible with TCC0 WO[3]
     *       for this implementation to work as-is.
     */
    ir_pwm(uint8_t pwm_pin, uint8_t sensor1_pin, uint8_t sensor2_pin);

    /**
     * @brief Initialize the driver.
     *
     * @param enable_sensor_1 Enable interrupt handling for sensor 1.
     * @param enable_sensor_2 Enable interrupt handling for sensor 2.
     * @param callback_sensor_1 Callback called on sensor 1 interrupt.
     * @param callback_sensor_2 Callback called on sensor 2 interrupt.
     */
    void begin(bool enable_sensor_1, bool enable_sensor_2, ir_isr_callback_t callback_sensor_1,
               ir_isr_callback_t callback_sensor_2);

    /**
     * @brief Start the IR PWM carrier.
     */
    void start_pwm();

    /**
     * @brief Stop the IR PWM carrier.
     */
    void stop_pwm();

    /**
     * @brief Update the callback used for sensor 1.
     *
     * @param callback New callback for sensor 1.
     */
    void set_callback_sensor_1(ir_isr_callback_t callback);

    /**
     * @brief Update the callback used for sensor 2.
     *
     * @param callback New callback for sensor 2.
     */
    void set_callback_sensor_2(ir_isr_callback_t callback);

   private:
    /**
     * @brief Singleton instance used by static ISR wrappers.
     */
    static ir_pwm* instance;

    uint8_t _pwm_pin;
    uint8_t _sensor1_pin;
    uint8_t _sensor2_pin;

    bool _enable_sensor_1 = false;
    bool _enable_sensor_2 = false;

    ir_isr_callback_t _callback_sensor_1 = nullptr;
    ir_isr_callback_t _callback_sensor_2 = nullptr;

    /**
     * @brief Configure the hardware PWM peripheral.
     */
    void setup_pwm();

    /**
     * @brief Configure GPIO interrupts for enabled sensors.
     */
    void setup_interrupts();

    /**
     * @brief Static ISR wrapper for sensor 1.
     */
    static void isr_sensor_1();

    /**
     * @brief Static ISR wrapper for sensor 2.
     */
    static void isr_sensor_2();

    /**
     * @brief Handle interrupt from sensor 1.
     */
    void handle_interrupt_sensor_1();

    /**
     * @brief Handle interrupt from sensor 2.
     */
    void handle_interrupt_sensor_2();
};
