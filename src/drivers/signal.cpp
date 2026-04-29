// #include "signal.h"
//
// constexpr uint8_t pin_led = 13;
// constexpr uint8_t pin_buzzer = 13; // Also supported: LED and buzzer may share the same pin.
//
// void setup()
// {
//     signal_engine_init(pin_led, pin_buzzer);
//
//     led_start_blink_isr(3, blink_mode::fast);
//     buzzer_beep_isr(200);
// }
//
// void loop()
// {
//     // Required for non-blocking LED / buzzer timing.
//     signal_engine_update();
// }

#include "signal.h"
#include "log.h"

void blink_blocking_safe(uint8_t pin, uint32_t on_ms, uint32_t off_ms, uint8_t repeat) {
    for (uint8_t i = 0; i < repeat; i++) {

        digitalWrite(pin, HIGH);
        for (uint32_t t = 0; t < on_ms; t += 10) {
            delay(10);
        }

        digitalWrite(pin, LOW);
        for (uint32_t t = 0; t < off_ms; t += 10) {
            delay(10);
        }
    }
}

/**
 * @brief Global LED channel state instance.
 */
static signal_channel_state_t s_led_channel;

/**
 * @brief Global buzzer channel state instance.
 */
static signal_channel_state_t s_buzzer_channel;

/**
 * @brief Runtime data used by the non-blocking scheduler.
 *
 * This deliberately stays outside signal_channel_state_t so the public struct
 * does not need to change. Timing is based on millis() and unsigned subtraction,
 * so it is safe across millis() wraparound.
 */
struct signal_channel_runtime_t {
    uint32_t start_ms           = 0;
    uint32_t last_transition_ms = 0;
};

static signal_channel_runtime_t s_led_runtime;
static signal_channel_runtime_t s_buzzer_runtime;

/**
 * @brief Return the global LED channel state.
 */
signal_channel_state_t& get_led_channel() {
    return s_led_channel;
}

/**
 * @brief Return the global buzzer channel state.
 */
signal_channel_state_t& get_buzzer_channel() {
    return s_buzzer_channel;
}

static signal_channel_runtime_t& get_runtime(signal_channel_state_t& channel) {
    if (&channel == &s_led_channel) {
        return s_led_runtime;
    }

    return s_buzzer_runtime;
}

/**
 * @brief True when LED and buzzer are wired to the same physical output.
 */
static bool outputs_share_pin() {
    return s_led_channel.pin == s_buzzer_channel.pin;
}

/**
 * @brief Return the logical output level currently requested by one channel.
 */
static bool channel_requests_high(const signal_channel_state_t& channel) {
    return channel.active && channel.level_high;
}

/**
 * @brief Apply the logical LED / buzzer states to the physical GPIO pin(s).
 *
 * If LED and buzzer share the same pin, the pin is HIGH when either logical
 * channel requests HIGH. This avoids one channel forcing the pin LOW while the
 * other one is still active.
 */
static void apply_signal_outputs() {
    const bool led_high    = channel_requests_high(s_led_channel);
    const bool buzzer_high = channel_requests_high(s_buzzer_channel);

    if (outputs_share_pin()) {
        digitalWrite(s_led_channel.pin, (led_high || buzzer_high) ? HIGH : LOW);
        return;
    }

    digitalWrite(s_led_channel.pin, led_high ? HIGH : LOW);
    digitalWrite(s_buzzer_channel.pin, buzzer_high ? HIGH : LOW);
}

/**
 * @brief Reset runtime timing for one output channel.
 */
static void reset_runtime(signal_channel_runtime_t& runtime) {
    runtime.start_ms           = 0;
    runtime.last_transition_ms = 0;
}

/**
 * @brief Reset one logical output channel to idle.
 *
 * This does not write the GPIO directly. Always call apply_signal_outputs()
 * after state changes so shared-pin configurations remain safe.
 */
static void reset_channel(signal_channel_state_t& channel) {
    channel.active       = false;
    channel.level_high   = false;
    channel.mode         = signal_mode::idle;
    channel.period_ms    = 0;
    channel.elapsed_ms   = 0;
    channel.duration_ms  = 0;
    channel.toggles_left = 0;

    reset_runtime(get_runtime(channel));
}

/**
 * @brief Stop one logical channel and refresh the physical GPIO output.
 */
static void stop_channel(signal_channel_state_t& channel) {
    reset_channel(channel);
    apply_signal_outputs();
    LOG_DEBUG("Channel on pin %u stopped", channel.pin);
}

/**
 * @brief Return true when duration_ms elapsed since start_ms.
 */
static bool elapsed(uint32_t now_ms, uint32_t start_ms, uint32_t duration_ms) {
    return static_cast<uint32_t>(now_ms - start_ms) >= duration_ms;
}

/**
 * @brief Update one logical channel without blocking and without disabling interrupts.
 *
 * This function updates state only. GPIO writes are done once by
 * signal_engine_update() through apply_signal_outputs().
 */
static void update_channel(signal_channel_state_t& channel) {
    if (!channel.active) {
        return;
    }

    signal_channel_runtime_t& runtime = get_runtime(channel);
    const uint32_t now_ms             = millis();

    if (channel.mode == signal_mode::pulse) {
        if (elapsed(now_ms, runtime.start_ms, channel.duration_ms)) {
            reset_channel(channel);
        }

        return;
    }

    if (channel.mode != signal_mode::blink) {
        return;
    }

    if (channel.period_ms == 0) {
        reset_channel(channel);
        return;
    }

    while (channel.active && elapsed(now_ms, runtime.last_transition_ms, channel.period_ms)) {
        runtime.last_transition_ms += channel.period_ms;

        if (channel.toggles_left == 0) {
            reset_channel(channel);
            return;
        }

        channel.level_high = !channel.level_high;
        channel.toggles_left--;

        // A full blink sequence always finishes LOW. Stop immediately after the
        // last transition instead of keeping the channel active for one extra
        // OFF period.
        if (channel.toggles_left == 0 && !channel.level_high) {
            reset_channel(channel);
            return;
        }
    }
}

/**
 * @brief Initialize the non-blocking signal engine and both output channels.
 *
 * No timer ISR is configured here. The caller must call signal_engine_update()
 * regularly from loop() or from the main state-machine tick.
 */
void signal_engine_init(uint32_t led_pin, uint32_t buzzer_pin) {

    signal_channel_state_t& led_channel    = get_led_channel();
    signal_channel_state_t& buzzer_channel = get_buzzer_channel();

    led_channel.pin = led_pin;
    reset_channel(led_channel);

    buzzer_channel.pin = buzzer_pin;
    reset_channel(buzzer_channel);

    pinMode(led_pin, OUTPUT);
    if (led_pin != buzzer_pin) {
        pinMode(buzzer_pin, OUTPUT);
    }

    apply_signal_outputs();
}

/**
 * @brief Update LED and buzzer timing.
 *
 * This function is non-blocking. It must be called often enough to get the
 * desired timing precision.
 */
void signal_engine_update() {
    update_channel(get_led_channel());
    update_channel(get_buzzer_channel());
    apply_signal_outputs();
}

/**
 * @brief Start a non-blocking LED blink sequence.
 *
 * The name is kept for compatibility with existing code, but this function no
 * longer uses an ISR timer and does not disable interrupts.
 *
 * One full blink = ON then OFF.
 */
void led_start_blink_isr(uint8_t count, blink_mode speed) {
    signal_channel_state_t& led_channel = get_led_channel();
    signal_channel_runtime_t& runtime   = get_runtime(led_channel);

    if (count == 0) {
        stop_channel(led_channel);
        return;
    }

    const uint32_t now_ms = millis();

    led_channel.active       = true;
    led_channel.level_high   = true;
    led_channel.mode         = signal_mode::blink;
    led_channel.period_ms    = static_cast<uint16_t>(speed);
    led_channel.elapsed_ms   = 0;
    led_channel.duration_ms  = 0;
    led_channel.toggles_left = static_cast<uint8_t>(count * 2U - 1U);

    runtime.start_ms           = now_ms;
    runtime.last_transition_ms = now_ms;

    apply_signal_outputs();
}

/**
 * @brief Stop the LED sequence and refresh the physical output.
 */
void led_stop_isr() {
    stop_channel(get_led_channel());
}

/**
 * @brief Start a non-blocking buzzer pulse.
 *
 * The name is kept for compatibility with existing code, but this function no
 * longer uses an ISR timer and does not disable interrupts.
 */
void buzzer_beep_isr(uint16_t duration_ms) {
    signal_channel_state_t& buzzer_channel = get_buzzer_channel();
    signal_channel_runtime_t& runtime      = get_runtime(buzzer_channel);

    if (duration_ms == 0) {
        stop_channel(buzzer_channel);
        return;
    }

    const uint32_t now_ms = millis();

    buzzer_channel.active       = true;
    buzzer_channel.level_high   = true;
    buzzer_channel.mode         = signal_mode::pulse;
    buzzer_channel.period_ms    = 0;
    buzzer_channel.elapsed_ms   = 0;
    buzzer_channel.duration_ms  = duration_ms;
    buzzer_channel.toggles_left = 0;

    runtime.start_ms           = now_ms;
    runtime.last_transition_ms = now_ms;

    apply_signal_outputs();
}

/**
 * @brief Stop the buzzer sequence and refresh the physical output.
 */
void buzzer_stop_isr() {
    stop_channel(get_buzzer_channel());
}

/**
 * @brief Compatibility stub.
 *
 * TC5 is no longer used by this non-blocking implementation. Keeping the symbol
 * avoids link errors if an old startup file still references it.
 */
void TC5_Handler() {}
