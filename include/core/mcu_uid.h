/**
 * @file mcu_uid.h
 * @brief Read and persist the unique Feather M0 MCU UID.
 */

#pragma once

/// Read Feather M0 unique ID and update `assembly.uid_mainboard` if needed.
void readFeatherUID();
