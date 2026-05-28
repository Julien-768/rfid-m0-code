/**
 * @file gui_serial.cpp
 * @brief GUI UART ownership and control helpers.
 *
 * This module manages the UART interface used by the external GUI during
 * CONNECTED mode.
 *
 * Responsibilities:
 * - Start GUI UART communication
 * - Stop and release the UART
 * - Centralize UART ownership switching logic
 *
 * The GUI UART may share the same hardware serial peripheral as other runtime
 * modules such as the RFID driver.
 */

#include "gui_serial.h"

#include "hardware_serial_control.h"
#include "log.h"

static constexpr uint32_t GUI_BAUDRATE = 115200;

/**
 * @brief Enable or disable GUI UART communication.
 *
 * When enabled:
 * - Restarts the UART interface
 * - Configures the GUI baudrate
 * - Flushes stale RX bytes
 *
 * When disabled:
 * - Flushes pending TX bytes
 * - Stops the UART peripheral
 *
 * This helper centralizes GUI UART ownership transitions.
 *
 * @param serial   Hardware serial interface used by the GUI.
 * @param enabled  Desired UART state.
 */
void gui_serial_set_enabled(HardwareSerial* serial, bool enabled) {
    if (!serial) {
        return;
    }

    if (enabled) {
        HardwareSerialControl::restart(serial, GUI_BAUDRATE, 100);
        HardwareSerialControl::flushRx(serial);

        LOG_DEBUG("GUI UART started");
    } else {
        HardwareSerialControl::stop(serial);

        LOG_DEBUG("GUI UART stopped");
    }
}

/**
 * @brief Start GUI communication on a hardware serial port.
 *
 * Initializes the UART for GUI communication during CONNECTED mode.
 *
 * @param serial Hardware serial port used by the GUI.
 */
void gui_serial_start(HardwareSerial* serial) {
    gui_serial_set_enabled(serial, true);
}

/**
 * @brief Stop GUI communication and release the UART.
 *
 * Stops the UART so it can safely be reassigned to another runtime mode,
 * such as RFID communication during DEPLOY mode.
 *
 * @param serial Hardware serial port used by the GUI.
 */
void gui_serial_stop(HardwareSerial* serial) {
    gui_serial_set_enabled(serial, false);
}
