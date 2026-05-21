#include "gui_serial.h"

#include "log.h"

static constexpr uint32_t GUI_BAUDRATE = 115200;

/**
 * @brief Start GUI communication on a hardware serial port.
 *
 * Initializes the UART for GUI communication during CONNECTED mode.
 *
 * @param serial Hardware serial port used by the GUI.
 */
void gui_serial_start(HardwareSerial* serial) {
    if (!serial) return;

    serial->end();
    delay(20);

    serial->begin(GUI_BAUDRATE);

    while (serial->available()) {
        serial->read();
    }

    LOG_INFO("GUI UART started");
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
    if (!serial) return;

    serial->flush();
    serial->end();

    LOG_INFO("GUI UART stopped");
}
