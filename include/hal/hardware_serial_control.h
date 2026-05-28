/**
 * @file hardware_serial_control.h
 * @brief Generic HardwareSerial control helpers.
 *
 * This module provides reusable helper functions to safely:
 * - stop a UART interface
 * - restart a UART interface
 * - flush pending RX bytes
 *
 * It is intended to centralize UART ownership transitions between runtime
 * modules sharing the same hardware serial peripheral (e.g. GUI and RFID).
 */

#pragma once

#include <Arduino.h>

namespace HardwareSerialControl {

/**
 * @brief Restart a hardware serial interface safely.
 *
 * Performs the following sequence:
 * - stop the UART peripheral
 * - wait a short stabilization delay
 * - restart the UART with the requested baudrate
 * - wait for UART stabilization
 *
 * This helper is useful when reassigning UART ownership between runtime
 * modules or after external device power cycling.
 *
 * @param serial     Hardware serial interface.
 * @param baudrate   UART baudrate.
 * @param settle_ms  Stabilization delay after restart in milliseconds.
 */
void restart(HardwareSerial* serial, uint32_t baudrate, uint32_t settle_ms = 100);

/**
 * @brief Stop a hardware serial interface safely.
 *
 * Flushes pending TX bytes before stopping the UART peripheral.
 *
 * @param serial Hardware serial interface.
 */
void stop(HardwareSerial* serial);

/**
 * @brief Flush all pending RX bytes from a hardware serial interface.
 *
 * Removes all currently available bytes from the UART RX buffer.
 *
 * Useful after:
 * - external peripheral startup
 * - UART reassignment
 * - line noise or partial frame reception
 *
 * @param serial Hardware serial interface.
 */
void flushRx(HardwareSerial* serial);

}  // namespace HardwareSerialControl
