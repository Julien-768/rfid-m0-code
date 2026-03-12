#include <Arduino.h>
#include "rfid_driver.h"

rfid_driver_t rfid1;
rfid_driver_t rfid2;  // optional second instance

tag_info_t prev1 = {{0}, 0};

void setup() {
    Serial.begin(115200);

    Serial1.begin(9600);
    rfid_driver_init(&rfid1, &Serial1, TAG_TYPE_FDX, 100);

    // If you have a second port/reader:
    // Serial2.begin(9600);
    // rfid_driver_init(&rfid2, &Serial2, TAG_TYPE_HDX, 150);
}

void loop() {
    rfid_driver_tick(&rfid1);
    // rfid_driver_tick(&rfid2);

    tag_info_t t;
    if (rfid_driver_get_tag(&rfid1, &t))
        {
            if (rfid_should_record_tag(&prev1, &t, 2000))
                {
                    Serial.print("RFID1: ");
                    Serial.println(t.tag);
                    prev1 = t;
            }
    }
}
