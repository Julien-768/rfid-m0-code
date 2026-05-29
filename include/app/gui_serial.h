/**
 * @file gui_serial.h
 * @brief GUI UART ownership and communication lifecycle helpers.
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Enable or disable GUI UART communication.
 *
 * @param serial   Hardware serial port used by the GUI.
 * @param enabled  true to start the UART, false to stop it.
 */
void gui_serial_set_enabled(HardwareSerial* serial, bool enabled);
