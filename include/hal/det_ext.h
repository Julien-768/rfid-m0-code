#pragma once
#include <Arduino.h>

#define PIN_DET_EXT 5  ///< GPIO D5 used to detect if FTDI/Serial1 is connected

/**
 * @brief Initialize the DET_EXT pin.
 *
 * Configures the DET_EXT pin as input with pull-up resistor.
 * The pin is expected to be pulled LOW externally when a
 * Serial1 adapter (FTDI) is connected.
 */
inline void DET_EXT_Init() {
    pinMode(PIN_DET_EXT, INPUT_PULLUP);
}

/**
 * @brief Check if FTDI/Serial1 is connected via DET_EXT pin.
 *
 * @return true if FTDI is connected (pin LOW),
 * @return false if not connected (pin HIGH).
 */
inline bool DET_EXT_Connected() {
    return (digitalRead(PIN_DET_EXT) == LOW);
}
