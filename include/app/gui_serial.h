/**
 * @file gui_serial.h
 * @brief GUI UART ownership and communication lifecycle helpers.
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Start GUI communication on a hardware serial port.
 *
 * @param serial Hardware serial port used by the GUI.
 */
void gui_serial_start(HardwareSerial* serial);

/**
 * @brief Stop GUI communication and release the UART.
 *
 * @param serial Hardware serial port used by the GUI.
 */
void gui_serial_stop(HardwareSerial* serial);
