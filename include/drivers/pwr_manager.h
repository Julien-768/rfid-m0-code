#pragma once

#include <stdint.h>

namespace pwr_manager {

/**
 * @brief Initialise les GPIO de power gating.
 *
 * À appeler une fois au boot.
 */
void begin();

/**
 * @brief Active le rail d'alimentation IR.
 */
void ir_power_on();

/**
 * @brief Coupe le rail d'alimentation IR.
 */
void ir_power_off();

/**
 * @brief Retourne l'état courant du rail IR.
 */
bool ir_is_on();

/**
 * @brief Active l'alimentation RFID.
 *
 * @param rfid_mode Mode runtime RFID.
 * @return true si l'alimentation a été activée.
 */
bool rfid_pwr_on(uint8_t rfid_mode);

/**
 * @brief Coupe l'alimentation RFID.
 *
 * @param rfid_mode Mode runtime RFID.
 */
void rfid_pwr_off(uint8_t rfid_mode);

/**
 * @brief Retourne l'état courant du rail RFID.
 */
bool rfid_is_on();

}  // namespace pwr_manager
