#include "Arduino.h"
#include "wiring_private.h"

void low_power_detach_interrupt(uint8_t pin) {
    detachInterrupt(digitalPinToInterrupt(pin));

    EExt_Interrupts in = g_APinDescription[pin].ulExtInt;
    if (in != NOT_AN_INTERRUPT && in != EXTERNAL_INT_NMI) {
        EIC->WAKEUP.reg &= ~(1 << in);
    }
}
