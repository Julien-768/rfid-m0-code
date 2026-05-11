#pragma once
#include <Arduino.h>

class SoftTx : public Print {
   public:
    SoftTx(uint8_t pin, uint32_t baud = 9600) : _pin(pin), _bit_us(1000000UL / baud) {}

    void begin() {
        pinMode(_pin, OUTPUT);
        digitalWrite(_pin, HIGH);  // idle UART
    }

    size_t write(uint8_t b) override {
        noInterrupts();

        // start bit
        digitalWrite(_pin, LOW);
        delayMicroseconds(_bit_us);

        // 8 data bits, LSB first
        for (uint8_t i = 0; i < 8; i++) {
            digitalWrite(_pin, (b & 0x01) ? HIGH : LOW);
            delayMicroseconds(_bit_us);
            b >>= 1;
        }

        // stop bit
        digitalWrite(_pin, HIGH);
        delayMicroseconds(_bit_us);

        interrupts();
        return 1;
    }

   private:
    uint8_t _pin;
    uint32_t _bit_us;
};
