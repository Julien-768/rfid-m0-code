#pragma once

#include <Arduino.h>

void blink_blocking_safe(uint8_t pin, uint32_t on_ms, uint32_t off_ms, uint8_t repeat);

/**
 * @brief Blink speed presets in milliseconds.
 *
 * The value is the ON/OFF transition period used by signal_engine_update().
 */
enum class blink_mode : uint16_t { slow = 500, medium = 250, fast = 100 };

/**
 * @brief Logical output channel mode.
 */
enum class signal_mode : uint8_t { idle = 0, pulse, blink };

/**
 * @brief Public state for one non-blocking output channel.
 *
 * The engine no longer uses a timer ISR and does not disable interrupts.
 * Call signal_engine_update() regularly from the main loop or scheduler.
 */
struct signal_channel_state_t {
    bool active      = false;  ///< True while a sequence is running.
    bool level_high  = false;  ///< Current logical output level requested by this channel.
    uint32_t pin     = 0;      ///< GPIO pin assigned to this channel.
    signal_mode mode = signal_mode::idle;

    uint16_t period_ms   = 0;  ///< Transition period for blink mode.
    uint16_t elapsed_ms  = 0;  ///< Kept for compatibility; runtime timing is handled internally.
    uint16_t duration_ms = 0;  ///< Duration for pulse mode.
    uint8_t toggles_left = 0;  ///< Remaining transitions for blink mode.
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
 * @brief Initialize the non-blocking signal engine and output pins.
 *
 * LED and buzzer may use different pins or share the same physical pin. When
 * both share a pin, the output is HIGH while either logical channel requests
 * HIGH.
 *
 * @param led_pin GPIO pin used for the LED.
 * @param buzzer_pin GPIO pin used for the buzzer.
 */
void signal_engine_init(uint32_t led_pin, uint32_t buzzer_pin);

/**
 * @brief Update LED and buzzer timing.
 *
 * This function is non-blocking and must be called regularly from loop(), a
 * scheduler tick, or the main state-machine update. Do not call it from an ISR.
 */
void signal_engine_update();

/**
 * @brief Start a non-blocking LED blink sequence.
 *
 * Name kept for compatibility with existing code; this implementation no
 * longer uses an ISR timer.
 *
 * @param count Number of full blinks. One blink = ON then OFF.
 * @param speed Blink speed preset.
 */
void led_start_blink_isr(uint8_t count, blink_mode speed);

/**
 * @brief Stop the LED sequence and force the LED logical output LOW.
 */
void led_stop_isr();

/**
 * @brief Start a non-blocking buzzer pulse.
 *
 * Name kept for compatibility with existing code; this implementation no
 * longer uses an ISR timer.
 *
 * @param duration_ms Pulse duration in milliseconds.
 */
void buzzer_beep_isr(uint16_t duration_ms);

/**
 * @brief Stop the buzzer sequence and force the buzzer logical output LOW.
 */
void buzzer_stop_isr();

/**
 * @brief Compatibility stub for old TC5 timer setups.
 *
 * TC5 is no longer used by the non-blocking signal engine.
 */
void TC5_Handler();
