/**
 * @file hardware_serial_control.cpp
 * @brief Generic HardwareSerial control helpers.
 */

#include "hardware_serial_control.h"

namespace HardwareSerialControl {

/**
 * @brief Restart a hardware serial interface safely.
 *
 * @param serial     Hardware serial interface.
 * @param baudrate   UART baudrate.
 * @param settle_ms  Stabilization delay after restart.
 */
void restart(HardwareSerial* serial, uint32_t baudrate, uint32_t settle_ms) {
    if (!serial) {
        return;
    }

    serial->end();
    delay(20);

    serial->begin(baudrate);
    delay(settle_ms);
}

/**
 * @brief Stop a hardware serial interface safely.
 *
 * @param serial Hardware serial interface.
 */
void stop(HardwareSerial* serial) {
    if (!serial) {
        return;
    }

    serial->flush();
    serial->end();
}

/**
 * @brief Flush all pending RX bytes from a hardware serial interface.
 *
 * @param serial Hardware serial interface.
 */
void flushRx(HardwareSerial* serial) {
    if (!serial) {
        return;
    }

    while (serial->available()) {
        serial->read();
    }
}

}  // namespace HardwareSerialControl
