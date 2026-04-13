
// #include "signal.h"

// constexpr uint8_t pin_led = 13;
// constexpr uint8_t pin_buzzer = 19;

// void setup()
// {
//     signal_engine_init(pin_led, pin_buzzer);

//     led_start_blink_isr(3, blink_mode::fast);
//     buzzer_beep_isr(200);
// }

// void loop()
// {
//     // Nothing required for LED or buzzer timing.
// }

#include "signal.h"
#include "wiring_private.h"
#include "log.h"

/**
 * @brief Global LED channel state instance.
 */
static signal_channel_state_t s_led_channel;

/**
 * @brief Global buzzer channel state instance.
 */
static signal_channel_state_t s_buzzer_channel;

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

/**
 * @brief Configure TC5 to generate a periodic 1 ms interrupt on SAMD21.
 */
static void signal_engine_timer_init_tc5() {
    PM->APBCMASK.reg |= PM_APBCMASK_TC5;

    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(GCM_TC4_TC5) | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_CLKEN;
    while (GCLK->STATUS.bit.SYNCBUSY) {}

    TC5->COUNT16.CTRLA.bit.ENABLE = 0;
    while (TC5->COUNT16.STATUS.bit.SYNCBUSY) {}

    TC5->COUNT16.CTRLA.bit.SWRST = 1;
    while (TC5->COUNT16.CTRLA.bit.SWRST || TC5->COUNT16.STATUS.bit.SYNCBUSY) {}

    TC5->COUNT16.CTRLA.reg = TC_CTRLA_MODE_COUNT16 | TC_CTRLA_WAVEGEN_MFRQ |
                             TC_CTRLA_PRESCALER_DIV64 | TC_CTRLA_PRESCSYNC_PRESC;

    while (TC5->COUNT16.STATUS.bit.SYNCBUSY) {}

    // 48 MHz / 64 = 750 kHz
    // 750 counts = 1 ms
    TC5->COUNT16.CC[0].reg = 750 - 1;
    while (TC5->COUNT16.STATUS.bit.SYNCBUSY) {}

    TC5->COUNT16.INTENSET.bit.MC0 = 1;

    NVIC_ClearPendingIRQ(TC5_IRQn);
    NVIC_SetPriority(TC5_IRQn, 3);
    NVIC_EnableIRQ(TC5_IRQn);

    TC5->COUNT16.CTRLA.bit.ENABLE = 1;
    while (TC5->COUNT16.STATUS.bit.SYNCBUSY) {}
}

/**
 * @brief Reset one output channel to the idle LOW state.
 *
 * @param channel Channel to reset.
 */
static void reset_channel(signal_channel_state_t& channel) {
    channel.active       = false;
    channel.level_high   = false;
    channel.mode         = signal_mode::idle;
    channel.period_ms    = 0;
    channel.elapsed_ms   = 0;
    channel.duration_ms  = 0;
    channel.toggles_left = 0;
}

/**
 * @brief Update one channel from the timer ISR.
 *
 * @param channel Channel state to update.
 */
static void update_channel_isr(signal_channel_state_t& channel) {
    if (!channel.active) {
        return;
    }

    channel.elapsed_ms++;

    if (channel.mode == signal_mode::pulse) {
        if (channel.elapsed_ms >= channel.duration_ms) {
            digitalWrite(channel.pin, LOW);
            reset_channel(channel);
        }

        return;
    }

    if (channel.mode == signal_mode::blink) {
        if (channel.elapsed_ms < channel.period_ms) {
            return;
        }

        channel.elapsed_ms = 0;

        if (channel.toggles_left == 0) {
            digitalWrite(channel.pin, LOW);
            reset_channel(channel);
            return;
        }

        channel.level_high = !channel.level_high;
        digitalWrite(channel.pin, channel.level_high ? HIGH : LOW);
        channel.toggles_left--;
    }
}

/**
 * @brief Initialize the shared ISR engine and both output channels.
 */
void signal_engine_init(uint32_t led_pin, uint32_t buzzer_pin) {
    signal_channel_state_t& led_channel    = get_led_channel();
    signal_channel_state_t& buzzer_channel = get_buzzer_channel();

    log_flush();
    noInterrupts();

    led_channel.pin = led_pin;
    reset_channel(led_channel);

    buzzer_channel.pin = buzzer_pin;
    reset_channel(buzzer_channel);

    interrupts();

    pinMode(led_pin, OUTPUT);
    digitalWrite(led_pin, LOW);

    pinMode(buzzer_pin, OUTPUT);
    digitalWrite(buzzer_pin, LOW);

    signal_engine_timer_init_tc5();
}

/**
 * @brief Start an ISR-driven LED blink sequence.
 *
 * One full blink = ON then OFF.
 */
void led_start_blink_isr(uint8_t count, blink_mode speed) {
    signal_channel_state_t& led_channel = get_led_channel();

    log_flush();
    noInterrupts();

    if (count == 0) {
        digitalWrite(led_channel.pin, LOW);
        reset_channel(led_channel);
        interrupts();
        return;
    }

    led_channel.active       = true;
    led_channel.level_high   = true;
    led_channel.mode         = signal_mode::blink;
    led_channel.period_ms    = static_cast<uint16_t>(speed);
    led_channel.elapsed_ms   = 0;
    led_channel.duration_ms  = 0;
    led_channel.toggles_left = static_cast<uint8_t>(count * 2U - 1U);

    digitalWrite(led_channel.pin, HIGH);

    interrupts();
}

/**
 * @brief Stop the LED sequence and force the output LOW.
 */
void led_stop_isr() {
    signal_channel_state_t& led_channel = get_led_channel();

    log_flush();
    noInterrupts();
    digitalWrite(led_channel.pin, LOW);
    reset_channel(led_channel);
    interrupts();
}

/**
 * @brief Start an ISR-driven buzzer pulse.
 */
void buzzer_beep_isr(uint16_t duration_ms) {
    signal_channel_state_t& buzzer_channel = get_buzzer_channel();

    log_flush();
    noInterrupts();

    if (duration_ms == 0) {
        digitalWrite(buzzer_channel.pin, LOW);
        reset_channel(buzzer_channel);
        interrupts();
        return;
    }

    buzzer_channel.active       = true;
    buzzer_channel.level_high   = true;
    buzzer_channel.mode         = signal_mode::pulse;
    buzzer_channel.period_ms    = 0;
    buzzer_channel.elapsed_ms   = 0;
    buzzer_channel.duration_ms  = duration_ms;
    buzzer_channel.toggles_left = 0;

    digitalWrite(buzzer_channel.pin, HIGH);

    interrupts();
}

/**
 * @brief Stop the buzzer sequence and force the output LOW.
 */
void buzzer_stop_isr() {
    signal_channel_state_t& buzzer_channel = get_buzzer_channel();

    log_flush();
    noInterrupts();
    digitalWrite(buzzer_channel.pin, LOW);
    reset_channel(buzzer_channel);
    interrupts();
}

/**
 * @brief TC5 ISR called every 1 ms.
 *
 * This ISR updates both the LED channel and the buzzer channel.
 */
void TC5_Handler() {
    if (TC5->COUNT16.INTFLAG.bit.MC0) {
        TC5->COUNT16.INTFLAG.bit.MC0 = 1;
    }

    update_channel_isr(get_led_channel());
    update_channel_isr(get_buzzer_channel());
}
