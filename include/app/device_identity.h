#pragma once
#include <Arduino.h>

/**
 * @brief Persistent factory identity stored in SAMD21 internal flash (FlashStorage).
 *
 * Fields are ASCII, null-terminated.
 */
struct device_identity_t {
    char manufacturer[16];
    char device_type[16];
    char date_fab[16];
    char serial_number[16];
};

static_assert(sizeof(device_identity_t) == 64,
              "device_identity_t size changed: update flash record format if needed.");

/**
 * @brief Initialize identity cache from flash.
 * @return true if valid identity loaded or defaults written, false on flash error.
 */
bool device_id_init();

/**
 * @brief Print the factory identity to the log.
 *
 * @param id Identity payload to print.
 */
void device_id_print(const device_identity_t& id);

/**
 * @brief Get cached identity (RAM).
 * If init failed, returns safe defaults in RAM.
 */
const device_identity_t& device_id_get();

/**
 * @brief Program a new identity into flash (factory).
 * @return true on success, false on write failure.
 */
bool device_id_program(const device_identity_t& id);

/**
 * @brief Factory reset to defaults (optional helper).
 */
bool device_id_resetDefaults();

void device_id_applyFromFields(const char* manufacturer,
                               const char* device_type,
                               const char* date_fab,
                               const char* serial_number);
