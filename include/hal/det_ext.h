#pragma once
#include <Arduino.h>
#include "hardware.h"
#include "log.h"

#ifndef PIN_DET_EXT
#define PIN_DET_EXT 5
#endif

/**
 * @brief Initialize the DET_EXT pin.
 *
 * Configures the DET_EXT pin as input with pull-up resistor.
 * The pin is expected to be pulled LOW externally when an FTDI
 * connection to a laptop is present, and HIGH when not connected.
 */
inline void DET_EXT_Init() {
    pinMode(PIN_DET_EXT, INPUT_PULLUP);
    // LOG_DEBUG("DET_EXT pin %d initialized as INPUT_PULLUP", PIN_DET_EXT);
}

/**
 * @brief Check if FTDI is connected via DET_EXT pin.
 *
 * @return true if FTDI is connected (pin LOW),
 * @return false if not connected (pin HIGH).
 */
inline bool DET_EXT_Connected() {
    bool isConnected = (digitalRead(PIN_DET_EXT) == LOW);
    if (isConnected) {
        LOG_DEBUG("DET_EXT: pin %d LOW", PIN_DET_EXT);
    } else {
        LOG_DEBUG("DET_EXT: pin %d HIGH", PIN_DET_EXT);
    }
    return isConnected;
}
