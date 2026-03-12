#include <Arduino.h>

#include "core/mcu_uid.h"
#include "core/assembly.h"
#include "core/log.h"

void readFeatherUID() {
    if (assembly.uid_mainboard.length() > 0)
        {
            LOG_INFO("Feather UID already present in assembly.cfg: %s", assembly.uid_mainboard.c_str());
            return;
    }

    const uint32_t uid[4] = {*(uint32_t*)0x0080A00C, *(uint32_t*)0x0080A040, *(uint32_t*)0x0080A044, *(uint32_t*)0x0080A048};

    char uid_str[64];
    snprintf(uid_str, sizeof(uid_str), "FeatherM0_0x%08lX_0x%08lX_0x%08lX_0x%08lX", uid[0], uid[1], uid[2], uid[3]);

    assembly.uid_mainboard = uid_str;

    LOG_INFO("Feather UID loaded into memory.");
    LOG_INFO("Feather M0 UID read: %08lX-%08lX-%08lX-%08lX", uid[0], uid[1], uid[2], uid[3]);
}
