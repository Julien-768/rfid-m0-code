/**
 * @file mcu_uid.h
 * @brief Read and persist the unique Feather M0 MCU UID.
 */

#pragma once
#include <Arduino.h>

/// Read Feather M0 unique ID
String mcu_uid_read();
