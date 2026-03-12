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
