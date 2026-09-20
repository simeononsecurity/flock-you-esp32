// Unit tests for MAC prefix, BLE device name, and manufacturer ID detection.
// Runs on the host with: pio test -e native
// No hardware or ESP32 toolchain required.

#include <unity.h>
#include "../../fy_detect.h"

// ── Flock high-confidence MAC tests ──────────────────────────────────────────

void test_flock_high_mac_known(void) {
    TEST_ASSERT_TRUE(fyCheckFlockHighMAC("70:c9:4e:aa:bb:cc"));
    TEST_ASSERT_TRUE(fyCheckFlockHighMAC("82:6b:f2:00:11:22"));  // DeFlockJoplin
    TEST_ASSERT_TRUE(fyCheckFlockHighMAC("b4:1e:52:de:ad:01"));  // Direct IEEE
    TEST_ASSERT_TRUE(fyCheckFlockHighMAC("04:0d:84:ab:cd:ef"));  // FS Ext Battery
    TEST_ASSERT_TRUE(fyCheckFlockHighMAC("b4:e3:f9:01:02:03"));  // FS Ext Battery
}

void test_flock_high_mac_case_insensitive(void) {
    TEST_ASSERT_TRUE(fyCheckFlockHighMAC("70:C9:4E:AA:BB:CC"));
    TEST_ASSERT_TRUE(fyCheckFlockHighMAC("B4:1E:52:00:11:22"));
    TEST_ASSERT_TRUE(fyCheckFlockHighMAC("82:6B:F2:AA:BB:CC"));
}

void test_flock_high_mac_all_prefixes(void) {
    for (size_t i = 0; i < FY_OUI_HIGH_COUNT; i++) {
        char mac[18];
        snprintf(mac, sizeof(mac), "%s:00:00:00", fy_oui_high[i]);
        TEST_ASSERT_TRUE_MESSAGE(fyCheckFlockHighMAC(mac), fy_oui_high[i]);
    }
}

void test_flock_high_mac_no_match(void) {
    TEST_ASSERT_FALSE(fyCheckFlockHighMAC("aa:bb:cc:dd:ee:ff"));
    TEST_ASSERT_FALSE(fyCheckFlockHighMAC("00:00:00:00:00:00"));
    // Contract mfr OUIs must NOT match the high-confidence table
    TEST_ASSERT_FALSE(fyCheckFlockHighMAC("f4:6a:dd:11:22:33"));
    TEST_ASSERT_FALSE(fyCheckFlockHighMAC("e8:d0:fc:aa:bb:cc"));
    // SoundThinking must NOT match the high-confidence table
    TEST_ASSERT_FALSE(fyCheckFlockHighMAC("d4:11:d6:aa:bb:cc"));
}

void test_flock_high_mac_null(void) {
    TEST_ASSERT_FALSE(fyCheckFlockHighMAC(nullptr));
}

// ── Contract-manufacturer MAC tests ──────────────────────────────────────────

void test_flock_mfr_mac_known(void) {
    TEST_ASSERT_TRUE(fyCheckFlockMfrMAC("f4:6a:dd:11:22:33"));  // Liteon
    TEST_ASSERT_TRUE(fyCheckFlockMfrMAC("f8:a2:d6:aa:bb:cc"));  // Liteon
    TEST_ASSERT_TRUE(fyCheckFlockMfrMAC("00:f4:8d:01:02:03"));  // USI
    TEST_ASSERT_TRUE(fyCheckFlockMfrMAC("e0:0a:f6:de:ad:be"));  // USI (PR#39)
}

void test_flock_mfr_mac_case_insensitive(void) {
    TEST_ASSERT_TRUE(fyCheckFlockMfrMAC("F4:6A:DD:11:22:33"));
    TEST_ASSERT_TRUE(fyCheckFlockMfrMAC("E0:0A:F6:DE:AD:BE"));
}

void test_flock_mfr_mac_not_in_high_list(void) {
    // Contract mfr OUIs must NOT match the high-confidence table
    TEST_ASSERT_FALSE(fyCheckFlockHighMAC("f4:6a:dd:11:22:33"));
    TEST_ASSERT_FALSE(fyCheckFlockHighMAC("e0:0a:f6:de:ad:be"));
}

void test_flock_mfr_mac_no_match(void) {
    TEST_ASSERT_FALSE(fyCheckFlockMfrMAC("aa:bb:cc:dd:ee:ff"));
    TEST_ASSERT_FALSE(fyCheckFlockMfrMAC("70:c9:4e:aa:bb:cc"));  // High-conf OUI
}

// ── SoundThinking MAC tests ───────────────────────────────────────────────────

void test_soundthinking_mac_known(void) {
    TEST_ASSERT_TRUE(fyCheckSoundThinkingMAC("d4:11:d6:aa:bb:cc"));
}

void test_soundthinking_mac_case_insensitive(void) {
    TEST_ASSERT_TRUE(fyCheckSoundThinkingMAC("D4:11:D6:AA:BB:CC"));
    TEST_ASSERT_TRUE(fyCheckSoundThinkingMAC("d4:11:D6:01:02:03"));
}

void test_soundthinking_mac_no_match(void) {
    TEST_ASSERT_FALSE(fyCheckSoundThinkingMAC("aa:bb:cc:dd:ee:ff"));
    TEST_ASSERT_FALSE(fyCheckSoundThinkingMAC("70:c9:4e:aa:bb:cc"));
    TEST_ASSERT_FALSE(fyCheckSoundThinkingMAC("f4:6a:dd:11:22:33"));
}

void test_soundthinking_not_in_other_lists(void) {
    TEST_ASSERT_FALSE(fyCheckFlockHighMAC("d4:11:d6:aa:bb:cc"));
    TEST_ASSERT_FALSE(fyCheckFlockMfrMAC("d4:11:d6:aa:bb:cc"));
}

// ── List isolation — cross-contamination tests ────────────────────────────────

void test_lists_are_independent(void) {
    // High-conf OUI must not appear in mfr or soundthinking
    TEST_ASSERT_FALSE(fyCheckFlockMfrMAC("70:c9:4e:aa:bb:cc"));
    TEST_ASSERT_FALSE(fyCheckSoundThinkingMAC("70:c9:4e:aa:bb:cc"));
    // Mfr OUI must not appear in high or soundthinking
    TEST_ASSERT_FALSE(fyCheckFlockHighMAC("f4:6a:dd:11:22:33"));
    TEST_ASSERT_FALSE(fyCheckSoundThinkingMAC("f4:6a:dd:11:22:33"));
    // SoundThinking OUI must not appear in high or mfr
    TEST_ASSERT_FALSE(fyCheckFlockHighMAC("d4:11:d6:aa:bb:cc"));
    TEST_ASSERT_FALSE(fyCheckFlockMfrMAC("d4:11:d6:aa:bb:cc"));
}

// ── BLE device name tests ─────────────────────────────────────────────────────

void test_ble_name_exact(void) {
    TEST_ASSERT_TRUE(fyCheckBLEName("FS Ext Battery"));
    TEST_ASSERT_TRUE(fyCheckBLEName("Penguin"));
    TEST_ASSERT_TRUE(fyCheckBLEName("Flock"));
    TEST_ASSERT_TRUE(fyCheckBLEName("Pigvision"));
    TEST_ASSERT_TRUE(fyCheckBLEName("Raven"));
}

void test_ble_name_substring(void) {
    TEST_ASSERT_TRUE(fyCheckBLEName("My Flock Device"));
    TEST_ASSERT_TRUE(fyCheckBLEName("Pigvision Controller v2"));
    TEST_ASSERT_TRUE(fyCheckBLEName("Raven Unit #5"));
    TEST_ASSERT_TRUE(fyCheckBLEName("Flock Safety Camera"));
}

void test_ble_name_case_insensitive(void) {
    TEST_ASSERT_TRUE(fyCheckBLEName("penguin"));
    TEST_ASSERT_TRUE(fyCheckBLEName("FLOCK"));
    TEST_ASSERT_TRUE(fyCheckBLEName("fs ext battery"));
    TEST_ASSERT_TRUE(fyCheckBLEName("RAVEN"));
}

void test_ble_name_no_match(void) {
    TEST_ASSERT_FALSE(fyCheckBLEName("Random BLE Device"));
    TEST_ASSERT_FALSE(fyCheckBLEName("iPhone 15"));
    TEST_ASSERT_FALSE(fyCheckBLEName("Nest Doorbell"));
}

void test_ble_name_null_and_empty(void) {
    TEST_ASSERT_FALSE(fyCheckBLEName(nullptr));
    TEST_ASSERT_FALSE(fyCheckBLEName(""));
}

// ── Firmware-default radio MAC tests (firmware-derived set, 2026-09-16) ──────
//
// These two full addresses are the QCA9377 factory defaults from the Flock
// camera firmware dump. They must match EXACTLY (full 6 bytes) — that
// precision is the whole point, since the bare 00:03:7f OUI they sit in is
// shared with a huge installed base of unrelated Atheros hardware.

void test_fw_default_mac_known(void) {
    TEST_ASSERT_TRUE(fyCheckFlockExactMAC("00:03:7f:50:00:01"));  // bdwlan30/fakeboar
    TEST_ASSERT_TRUE(fyCheckFlockExactMAC("00:03:7f:4f:00:16"));  // otp30
}

void test_fw_default_mac_case_insensitive(void) {
    TEST_ASSERT_TRUE(fyCheckFlockExactMAC("00:03:7F:50:00:01"));
    TEST_ASSERT_TRUE(fyCheckFlockExactMAC("00:03:7F:4F:00:16"));
}

void test_fw_default_mac_requires_full_match(void) {
    // Same OUI, different suffix — must NOT be treated as a firmware default.
    TEST_ASSERT_FALSE(fyCheckFlockExactMAC("00:03:7f:50:00:02"));
    TEST_ASSERT_FALSE(fyCheckFlockExactMAC("00:03:7f:4f:00:17"));
    TEST_ASSERT_FALSE(fyCheckFlockExactMAC("00:03:7f:aa:bb:cc"));
}

void test_fw_default_mac_no_match(void) {
    TEST_ASSERT_FALSE(fyCheckFlockExactMAC("aa:bb:cc:dd:ee:ff"));
    TEST_ASSERT_FALSE(fyCheckFlockExactMAC("70:c9:4e:00:00:01"));
    TEST_ASSERT_FALSE(fyCheckFlockExactMAC(nullptr));
}

void test_fw_default_mac_count(void) {
    TEST_ASSERT_EQUAL(2u, (unsigned)FY_EXACT_MAC_COUNT);
}

// The Qualcomm Atheros OUI must be mfr-tier ONLY: it is a chipset vendor's
// prefix, so a bare 00:03:7f match is weak evidence, unlike b4:1e:52.
void test_qca_oui_is_mfr_tier_only(void) {
    TEST_ASSERT_TRUE(fyCheckFlockMfrMAC("00:03:7f:aa:bb:cc"));
    TEST_ASSERT_FALSE(fyCheckFlockHighMAC("00:03:7f:aa:bb:cc"));
}

// ── BLE name PATTERN tests (shape-matched, not substring) ────────────────────

void test_ble_name_pattern_penguin_serial(void) {
    TEST_ASSERT_TRUE(fyCheckBleNamePattern("Penguin-1234567890"));
    TEST_ASSERT_TRUE(fyCheckBleNamePattern("PENGUIN-0000000000"));
}

void test_ble_name_pattern_bare_serial(void) {
    // A bare 10-digit serial has no keyword to substring-match — this is the
    // only form the pattern matcher can catch.
    TEST_ASSERT_TRUE(fyCheckBleNamePattern("1234567890"));
    TEST_ASSERT_FALSE(fyCheckBLEName("1234567890"));     // needs the pattern path
    TEST_ASSERT_TRUE(fyCheckFlockBleName("1234567890")); // combined entry point catches it
}

void test_ble_name_pattern_dfutarg(void) {
    TEST_ASSERT_TRUE(fyCheckBleNamePattern("DfuTarg"));
    TEST_ASSERT_TRUE(fyCheckBleNamePattern("dfutarg"));
    TEST_ASSERT_TRUE(fyCheckFlockBleName("DfuTarg"));
}

void test_ble_name_pattern_fs_ext_battery_exact(void) {
    TEST_ASSERT_TRUE(fyCheckBleNamePattern("FS Ext Battery"));
    TEST_ASSERT_TRUE(fyCheckBleNamePattern("fs ext battery"));
}

void test_ble_name_pattern_rejects_wrong_digit_counts(void) {
    TEST_ASSERT_FALSE(fyCheckBleNamePattern("123456789"));     // 9 digits
    TEST_ASSERT_FALSE(fyCheckBleNamePattern("12345678901"));   // 11 digits
    TEST_ASSERT_FALSE(fyCheckBleNamePattern("1234567890X"));   // trailing junk
    TEST_ASSERT_FALSE(fyCheckBleNamePattern("Penguin-123456789"));
    TEST_ASSERT_FALSE(fyCheckBleNamePattern("Penguin-abcdefghij"));
}

void test_ble_name_pattern_no_match(void) {
    TEST_ASSERT_FALSE(fyCheckBleNamePattern("Random BLE Device"));
    TEST_ASSERT_FALSE(fyCheckBleNamePattern(""));
    TEST_ASSERT_FALSE(fyCheckBleNamePattern(nullptr));
}

void test_ble_name_keyword_and_pattern_both_work(void) {
    TEST_ASSERT_TRUE(fyCheckFlockBleName("Flock-Camera-01"));   // substring form
    TEST_ASSERT_TRUE(fyCheckFlockBleName("FS Ext Battery"));
    TEST_ASSERT_TRUE(fyCheckFlockBleName("9876543210"));       // pattern form
    TEST_ASSERT_FALSE(fyCheckFlockBleName("JBL Flip 5"));
    TEST_ASSERT_FALSE(fyCheckFlockBleName(nullptr));
}

// ── Flock accessory / Nordic DFU GATT service tests ──────────────────────────

void test_flock_gatt_uuid_known(void) {
    const char* accessory[] = { FY_FLOCK_ACCESSORY_UUID };
    const char* dfu[]       = { FY_NORDIC_DFU_UUID };
    TEST_ASSERT_TRUE(fyCheckFlockGattUUIDFromStrings(accessory, 1, nullptr));
    TEST_ASSERT_TRUE(fyCheckFlockGattUUIDFromStrings(dfu, 1, nullptr));
}

void test_flock_gatt_uuid_case_insensitive(void) {
    const char* uuids[] = { "E8CCBB38-9532-46A8-9FE5-1814DF172E6F" };
    TEST_ASSERT_TRUE(fyCheckFlockGattUUIDFromStrings(uuids, 1, nullptr));
}

void test_flock_gatt_uuid_out_param(void) {
    const char* uuids[] = { FY_FLOCK_ACCESSORY_UUID };
    char out[41] = {0};
    TEST_ASSERT_TRUE(fyCheckFlockGattUUIDFromStrings(uuids, 1, out));
    TEST_ASSERT_EQUAL_STRING(FY_FLOCK_ACCESSORY_UUID, out);
}

void test_flock_gatt_uuid_no_match(void) {
    const char* uuids[] = { "12345678-1234-1234-1234-123456789abc" };
    TEST_ASSERT_FALSE(fyCheckFlockGattUUIDFromStrings(uuids, 1, nullptr));
    TEST_ASSERT_FALSE(fyCheckFlockGattUUIDFromStrings(nullptr, 0, nullptr));
}

void test_flock_gatt_uuid_not_raven(void) {
    // The Flock accessory service must NOT be reported as a Raven UUID — they
    // are deliberately separate alert types with separate method strings.
    const char* uuids[] = { FY_FLOCK_ACCESSORY_UUID };
    TEST_ASSERT_FALSE(fyCheckRavenUUIDFromStrings(uuids, 1, nullptr));
}

void test_flock_gatt_uuid_count(void) {
    TEST_ASSERT_EQUAL(2u, (unsigned)FY_BLE_GATT_UUID_COUNT);
}

// ── BLE manufacturer ID tests ─────────────────────────────────────────────────

void test_ble_mfr_id_known(void) {
    TEST_ASSERT_TRUE(fyCheckBLEMfrID(0x09C8));   // XUNTONG (confirmed Flock)
}

void test_ble_mfr_id_old_incorrect_value(void) {
    // 0x05A7 was the incorrect ID used in pre-PR#39 firmware — must NOT match
    TEST_ASSERT_FALSE(fyCheckBLEMfrID(0x05A7));
}

void test_ble_mfr_id_no_match(void) {
    TEST_ASSERT_FALSE(fyCheckBLEMfrID(0x0000));
    TEST_ASSERT_FALSE(fyCheckBLEMfrID(0xFFFF));
    TEST_ASSERT_FALSE(fyCheckBLEMfrID(0x004C));   // Apple
    TEST_ASSERT_FALSE(fyCheckBLEMfrID(0x0075));   // Samsung
}

// ─────────────────────────────────────────────────────────────────────────────

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_flock_high_mac_known);
    RUN_TEST(test_flock_high_mac_case_insensitive);
    RUN_TEST(test_flock_high_mac_all_prefixes);
    RUN_TEST(test_flock_high_mac_no_match);
    RUN_TEST(test_flock_high_mac_null);

    RUN_TEST(test_flock_mfr_mac_known);
    RUN_TEST(test_flock_mfr_mac_case_insensitive);
    RUN_TEST(test_flock_mfr_mac_not_in_high_list);
    RUN_TEST(test_flock_mfr_mac_no_match);

    RUN_TEST(test_soundthinking_mac_known);
    RUN_TEST(test_soundthinking_mac_case_insensitive);
    RUN_TEST(test_soundthinking_mac_no_match);
    RUN_TEST(test_soundthinking_not_in_other_lists);

    RUN_TEST(test_lists_are_independent);

    RUN_TEST(test_fw_default_mac_known);
    RUN_TEST(test_fw_default_mac_case_insensitive);
    RUN_TEST(test_fw_default_mac_requires_full_match);
    RUN_TEST(test_fw_default_mac_no_match);
    RUN_TEST(test_fw_default_mac_count);
    RUN_TEST(test_qca_oui_is_mfr_tier_only);

    RUN_TEST(test_ble_name_exact);
    RUN_TEST(test_ble_name_substring);
    RUN_TEST(test_ble_name_case_insensitive);
    RUN_TEST(test_ble_name_no_match);
    RUN_TEST(test_ble_name_null_and_empty);

    RUN_TEST(test_ble_name_pattern_penguin_serial);
    RUN_TEST(test_ble_name_pattern_bare_serial);
    RUN_TEST(test_ble_name_pattern_dfutarg);
    RUN_TEST(test_ble_name_pattern_fs_ext_battery_exact);
    RUN_TEST(test_ble_name_pattern_rejects_wrong_digit_counts);
    RUN_TEST(test_ble_name_pattern_no_match);
    RUN_TEST(test_ble_name_keyword_and_pattern_both_work);

    RUN_TEST(test_flock_gatt_uuid_known);
    RUN_TEST(test_flock_gatt_uuid_case_insensitive);
    RUN_TEST(test_flock_gatt_uuid_out_param);
    RUN_TEST(test_flock_gatt_uuid_no_match);
    RUN_TEST(test_flock_gatt_uuid_not_raven);
    RUN_TEST(test_flock_gatt_uuid_count);

    RUN_TEST(test_ble_mfr_id_known);
    RUN_TEST(test_ble_mfr_id_old_incorrect_value);
    RUN_TEST(test_ble_mfr_id_no_match);

    return UNITY_END();
}
