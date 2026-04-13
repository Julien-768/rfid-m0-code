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
    bool isConnected = (digitalRead(PIN_DET_EXT) == LOW);
    if (isConnected) {
        LOG_DEBUG("DET_EXT: FTDI detected (pin %d LOW)", PIN_DET_EXT);
    } else {
        LOG_DEBUG("DET_EXT: No FTDI detected (pin %d HIGH)", PIN_DET_EXT);
    }
    return isConnected;
}
