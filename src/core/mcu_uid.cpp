/*
 * mcu_uid.cpp - Read MCU unique ID for Feather M0
 *
 */

#include "mcu_uid.h"
#include "log.h"

String mcu_uid_read() {
    LOG_DEBUG("Reading Feather M0 UID from internal flash...");

    const uint32_t uid[4] = {*(uint32_t*)0x0080A00C, *(uint32_t*)0x0080A040, *(uint32_t*)0x0080A044, *(uint32_t*)0x0080A048};

    char uid_str[64];
    snprintf(uid_str, sizeof(uid_str), "FeatherM0_0x%08lX_0x%08lX_0x%08lX_0x%08lX", uid[0], uid[1], uid[2], uid[3]);
    LOG_DEBUG("Feather M0 UID read: %08lX-%08lX-%08lX-%08lX", uid[0], uid[1], uid[2], uid[3]);

    return uid_str;
}
