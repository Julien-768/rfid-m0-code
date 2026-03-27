#pragma once

#include <Arduino.h>

/**
 * @brief Blink speed presets in milliseconds.
 */
enum class blink_mode : uint16_t { slow = 500, medium = 250, fast = 100 };

/**
 * @brief Output channel mode.
 */
enum class signal_mode : uint8_t { idle = 0, pulse, blink };

/**
 * @brief State for one ISR-driven output channel.
 *
 * This structure is shared between the main code and the timer ISR.
 */
struct signal_channel_state_t {
    volatile bool active      = false;  ///< True while a sequence is running.
    volatile bool level_high  = false;  ///< Current GPIO level.
    volatile uint32_t pin     = 0;      ///< GPIO pin assigned to this channel.
    volatile signal_mode mode = signal_mode::idle;

    volatile uint16_t period_ms   = 0;  ///< Toggle period for blink mode.
    volatile uint16_t elapsed_ms  = 0;  ///< Elapsed time since last state change.
    volatile uint16_t duration_ms = 0;  ///< Duration for pulse mode.
    volatile uint8_t toggles_left = 0;  ///< Remaining toggles for blink mode.
};

/**
 * @brief Return the global LED channel state.
 *
 * @return Reference to the LED channel state.
 */
signal_channel_state_t& get_led_channel();

/**
 * @brief Return the global buzzer channel state.
 *
 * @return Reference to the buzzer channel state.
 */
signal_channel_state_t& get_buzzer_channel();

/**
 * @brief Initialize the shared ISR engine and output pins.
 *
 * @param led_pin GPIO pin used for the LED.
 * @param buzzer_pin GPIO pin used for the buzzer.
 */
void signal_engine_init(uint32_t led_pin, uint32_t buzzer_pin);

/**
 * @brief Start a non-blocking ISR-driven LED blink sequence.
 *
 * @param count Number of full blinks.
 * @param speed Blink speed preset.
 */
void led_start_blink_isr(uint8_t count, blink_mode speed);

/**
 * @brief Stop the LED sequence and force the LED output LOW.
 */
void led_stop_isr();

/**
 * @brief Start a non-blocking ISR-driven buzzer pulse.
 *
 * @param duration_ms Pulse duration in milliseconds.
 */
void buzzer_beep_isr(uint16_t duration_ms);

/**
 * @brief Stop the buzzer sequence and force the buzzer output LOW.
 */
void buzzer_stop_isr();
