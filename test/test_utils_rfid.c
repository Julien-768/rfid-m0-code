/**
 * @file test_utils_rfid.c
 *
 * @version 3.0.0
 * @date 2022-08-17
 *
 * @section authors Authors
 * - Julien Courtecuisse
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: julien.courtecuisse@iphc.cnrs.fr
 *
 * @section project Project
 * Name: RFID-M0
 * Description: Embedded firmware for the open-source RFID-M0 project, designed to monitor small animal species around burrows and nest boxes. The software integrates RFID-based individual identification, infrared passage detection, real-time clock (RTC) timestamping, and SD-card data logging, with low-power management for autonomous field deployments. It supports ecological research, including long-term monitoring of Great Tits (Parus major) and research on the European hamster (Cricetus cricetus) conducted within the cross-border CRICETUS project.
 *
 * @section contributors Contributors
 * - Robin Laesser
 *   Affiliation: IPHC-DEPE; UMR 7178; Université de Strasbourg-CNRS; F-67000 Strasbourg, France
 *   Email: robin.laesser@iphc.cnrs.fr
 *
 * @section license License
 * Copyright (C) 2022 Julien Courtecuisse
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * License: GPL-3.0-or-later
 * https://www.gnu.org/licenses/gpl-3.0.html
 */

/* test_utils_rfid.c */
#include "unity.h"
#include "drivers/utils_rfid.h"
#include <string.h>
#include <stdio.h>

void setUp(void) {
    // Configuration avant chaque test (si nécessaire)
}

void tearDown(void) {
    // Nettoyage après chaque test (si nécessaire)
}

/* ------------------------------------------------------------------
 * Test 1: Conversion d'une balise FDX hexadécimale en NIC
 * ------------------------------------------------------------------ */
void test_rfid_tag_hex_to_nic(void) {
    char buffer[RFID_NIC_STR_MAX];
    const char* tag_hex      = "80003EB533A9F1FA";
    const char* expected_nic = "250-228500042234";

    TEST_ASSERT_TRUE(rfid_tag_hex_to_nic(tag_hex, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING(expected_nic, buffer);
}

/* ------------------------------------------------------------------
 * Test 2: compare_last10() doit correspondre aux suffixes identiques
 * ------------------------------------------------------------------ */
void test_rfid_compare_last10_match(void) {
    TEST_ASSERT_TRUE(rfid_compare_last10("AAAAA0123456789", "BBBBB0123456789"));
}

/* ------------------------------------------------------------------
 * Test 3: compare_last10() doit détecter une non-correspondance
 * ------------------------------------------------------------------ */
void test_rfid_compare_last10_mismatch(void) {
    TEST_ASSERT_FALSE(rfid_compare_last10("AAAAA0123456789", "BBBBB0123459999"));
}

/* ------------------------------------------------------------------
 * Point d'entrée pour les tests Unity
 * ------------------------------------------------------------------ */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rfid_tag_hex_to_nic);
    RUN_TEST(test_rfid_compare_last10_match);
    RUN_TEST(test_rfid_compare_last10_mismatch);
    UNITY_END();
    return 0;
}
