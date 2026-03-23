/**
 * @file mcu_uid.h
 * @brief Read and persist the unique Feather M0 MCU UID.
 */

#pragma once

/// Read Feather M0 unique ID and update `hw_assembly.uid_mainboard` if needed.
// TODO should return uid instead of modifying global variable
void readFeatherUID();
