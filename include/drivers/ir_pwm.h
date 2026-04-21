#ifndef IR_PWM_H
#define IR_PWM_H

#include <Arduino.h>
#include "hardware.h"

#ifndef PWM_TIMER
#error "PWM_TIMER must be defined in hardware.h"
#endif

/**
 * @brief ISR callback type for IR sensors.
 *
 * The callback receives the current logic state read on the sensor pin.
 */
typedef void (*ir_isr_callback_t)(uint8_t state);

/**
 * @brief IR PWM driver for SAMD21 using ocrdu TurboPWM.
 *
 * Features:
 * - Hardware PWM carrier for an IR emitter
 * - Approx. 36 kHz carrier frequency
 * - Approx. 50% duty cycle
 * - Optional interrupt handling for up to 2 sensors
 * - Runtime start/stop of the carrier
 *
 * Notes:
 * - This implementation targets Adafruit Feather M0 / ATSAMD21G18.
 * - Pin 11 uses timer 2 in the ocrdu library.
 * - Pin 9 uses timer 1 in the ocrdu library.
 */
class ir_pwm {
   public:
    static constexpr uint32_t PWM_FREQUENCY_HZ = 36000;

    // ocrdu TurboPWM uses duty values in the range [0..1000].
    static constexpr uint16_t PWM_DUTY_ON  = 500;  // ~50%
    static constexpr uint16_t PWM_DUTY_OFF = 0;

    // Feather M0 mapping for pin 11: timer 2
    // static constexpr uint8_t PWM_TIMER = 2;

    // Timer configuration chosen for ~36 kHz:
    // f ≈ 48 MHz / (prescaler * steps)
    static constexpr uint8_t PWM_PRESCALER = 1;
    static constexpr uint16_t PWM_STEPS    = 1333;

    ir_pwm(uint8_t pwm_pin, uint8_t sensor1_pin, uint8_t sensor2_pin);

    void begin(bool enable_sensor_1, bool enable_sensor_2,
               ir_isr_callback_t callback_sensor_1 = nullptr,
               ir_isr_callback_t callback_sensor_2 = nullptr);

    void start_pwm();
    void stop_pwm();
    bool is_pwm_running() const { return _pwm_running; }

    void set_callback_sensor_1(ir_isr_callback_t callback);
    void set_callback_sensor_2(ir_isr_callback_t callback);
    void enable_sensor_wakeups();
    void disable_sensor_wakeups();

   private:
    bool _pwm_running = false;

    void setup_pwm();

    void handle_interrupt_sensor_1();
    void handle_interrupt_sensor_2();

    static void isr_sensor_1();
    static void isr_sensor_2();

   private:
    uint8_t _pwm_pin;
    uint8_t _sensor1_pin;
    uint8_t _sensor2_pin;

    bool _enable_sensor_1 = false;
    bool _enable_sensor_2 = false;

    ir_isr_callback_t _callback_sensor_1 = nullptr;
    ir_isr_callback_t _callback_sensor_2 = nullptr;

    static ir_pwm* instance;
};

#endif
