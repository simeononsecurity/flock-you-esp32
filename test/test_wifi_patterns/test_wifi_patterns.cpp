// Unit tests for the WiFi-side pattern data in fy_detect.h: the OUI tier
// tables, the SSID keyword list, the BLE manufacturer-ID table, and the
// Probe Request IE fingerprint.
// Runs on the host with: pio test -e native
// No hardware or ESP32 toolchain required.

#include <unity.h>
#include "../../fy_detect.h"

// ── helpers ──────────────────────────────────────────────────────────────────

static bool ouiInTable(const char* needle, const char* const* table,
                       size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (strcasecmp(table[i], needle) == 0) return true;
    }
    return false;
}

// ── OUI tier tables ──────────────────────────────────────────────────────────

// 14:b5:cd (Liteon) was missing from BOTH detection tables — the community
// dataset carries 32 prefixes and this repo only had 31. It belongs in the
// contract-manufacturer tier, not the high tier, because Liteon hardware is
// shared with unrelated consumer products (the same reasoning that puts
// f4:6a:dd / f8:a2:d6 there).
void test_oui_14b5cd_in_mfr_tier_only(void) {
    TEST_ASSERT_TRUE(ouiInTable("14:b5:cd", fy_oui_mfr, FY_OUI_MFR_COUNT));
    TEST_ASSERT_FALSE(ouiInTable("14:b5:cd", fy_oui_high, FY_OUI_HIGH_COUNT));
}

// Deliberate tripwires: these counts only change when a pattern is added or
// removed on purpose, so a silent edit to a table fails here.
void test_oui_tier_counts(void) {
    TEST_ASSERT_EQUAL(33u, (unsigned)FY_OUI_HIGH_COUNT);
    TEST_ASSERT_EQUAL(8u,  (unsigned)FY_OUI_MFR_COUNT);
}

// ── BLE manufacturer company ID ──────────────────────────────────────────────

void test_ble_mfr_id_table(void) {
    TEST_ASSERT_TRUE(fyCheckBLEMfrID(0x09C8));    // XUNTONG (confirmed Flock)
    TEST_ASSERT_FALSE(fyCheckBLEMfrID(0x05A7));   // the pre-PR#39 wrong value
    TEST_ASSERT_FALSE(fyCheckBLEMfrID(0x0000));
    TEST_ASSERT_EQUAL(1u, (unsigned)FY_BLE_MFR_COUNT);
    // ble_selftest.h derives its test advertisement from this slot.
    TEST_ASSERT_EQUAL(0x09C8, fy_ble_mfr_ids[0]);
}

// ── SSID keywords ────────────────────────────────────────────────────────────

void test_ssid_keyword_flock_forms(void) {
    TEST_ASSERT_TRUE(fyCheckFlockSsidKeyword("Flock"));
    TEST_ASSERT_TRUE(fyCheckFlockSsidKeyword("Flock-1A2B3C"));   // provisioning SoftAP
    TEST_ASSERT_TRUE(fyCheckFlockSsidKeyword("FLOCK-1A2B3C"));   // case-insensitive
    TEST_ASSERT_TRUE(fyCheckFlockSsidKeyword("Flock Camera net."));
    TEST_ASSERT_TRUE(fyCheckFlockSsidKeyword("flocksafety"));
    TEST_ASSERT_TRUE(fyCheckFlockSsidKeyword("FS Ext Battery"));
    TEST_ASSERT_TRUE(fyCheckFlockSsidKeyword("Penguin-1234567890"));
    TEST_ASSERT_TRUE(fyCheckFlockSsidKeyword("pigvision"));
}

// CVE-2025-59409: Flock's Falcon/Sparrow firmware ships development Wi-Fi
// credentials ("test_flck") in cleartext in production firmware. The truncated
// spelling does NOT contain "flock" (f-l-c-k vs f-l-o-c-k), so it needs its own
// keyword or such a camera is invisible to the SSID path.
void test_ssid_keyword_test_flck_cve(void) {
    TEST_ASSERT_FALSE(fySubstrCI("test_flck", "flock"));   // why a 2nd entry exists
    TEST_ASSERT_TRUE(fySubstrCI("test_flck", "flck"));
    TEST_ASSERT_TRUE(fyCheckFlockSsidKeyword("test_flck"));
    TEST_ASSERT_TRUE(fyCheckFlockSsidKeyword("flck"));
    // Transposed spelling is NOT a match — "flkc" != "flck".
    TEST_ASSERT_FALSE(fyCheckFlockSsidKeyword("TEST_FLKC"));
}

// "flck" is short, so confirm it did not become a catch-all.
void test_ssid_keyword_negatives(void) {
    TEST_ASSERT_FALSE(fyCheckFlockSsidKeyword("MyHomeWiFi"));
    TEST_ASSERT_FALSE(fyCheckFlockSsidKeyword("flick"));       // f-l-i-c-k
    TEST_ASSERT_TRUE(fyCheckFlockSsidKeyword("flockyou"));     // contains "flock"
    TEST_ASSERT_FALSE(fyCheckFlockSsidKeyword("FS Ext"));      // partial, not a keyword
    TEST_ASSERT_FALSE(fyCheckFlockSsidKeyword(""));
    TEST_ASSERT_FALSE(fyCheckFlockSsidKeyword(nullptr));
    TEST_ASSERT_FALSE(fyCheckFlockSsidKeyword("PRINTER-3F"));
}

// Every keyword in the table must match itself, so a typo added to the table
// cannot silently become dead data.
void test_every_ssid_keyword_matches_itself(void) {
    for (size_t i = 0; fy_ssid_keywords[i]; i++) {
        TEST_ASSERT_TRUE_MESSAGE(
            fyCheckFlockSsidKeyword(fy_ssid_keywords[i]), fy_ssid_keywords[i]);
    }
    TEST_ASSERT_EQUAL(6u, (unsigned)FY_SSID_KEYWORD_COUNT);
    // The count macro must exclude the nullptr terminator.
    TEST_ASSERT_EQUAL(FY_SSID_KEYWORD_COUNT + 1,
                      sizeof(fy_ssid_keywords)/sizeof(fy_ssid_keywords[0]));
}

// ── Probe Request IE fingerprint ─────────────────────────────────────────────

// A Probe Request body whose IE chain produces exactly the drive-tested
// signature: zero-length SSID, tags 2/12/127, the LiteON vendor IE (OUI
// 50:6f:9a), tags 45/191, then a second vendor IE.
static const uint8_t kFlockIeProbe[] = {
    0x00, 0x00,                                            // SSID, zero length
    0x02, 0x01, 0xAA,                                      // tag 2
    0x0C, 0x01, 0xBB,                                      // tag 12
    0x7F, 0x01, 0xCC,                                      // tag 127
    0xDD, 0x07, 0x50, 0x6f, 0x9a, 0x16, 0x03, 0x01, 0x03,  // vendor 221 (LiteON)
    0x2D, 0x01, 0xEE,                                      // tag 45
    0xBF, 0x01, 0xFF,                                      // tag 191
    0xDD, 0x07, 0x00, 0x50, 0xf2, 0x08, 0x00, 0x00, 0x00,  // vendor 221
};

// The encoder must reproduce the exact allowlist string — this is what proves
// the TLV walk, the SSID skip, and the "221:" + 8-byte hex encoding are right.
void test_ie_sig_encoding_matches_allowlist_string(void) {
    char sig[FY_IE_SIG_MAX] = {0};
    TEST_ASSERT_TRUE(fyIeSigFromProbeBody(kFlockIeProbe, sizeof(kFlockIeProbe),
                                          sig, sizeof(sig)));
    TEST_ASSERT_EQUAL_STRING(fy_ie_sig_allowlist[0], sig);
    TEST_ASSERT_EQUAL_STRING(
        "2,12,127,221:506f9a16030103,45,191,221:0050f208000000", sig);
}

void test_ie_sig_matches_drive_tested_probe(void) {
    TEST_ASSERT_TRUE(
        fyCheckFlockIeSignature(kFlockIeProbe, sizeof(kFlockIeProbe)));
}

// A different Flock OUI in the vendor IE is a different firmware: no match.
void test_ie_sig_rejects_wrong_vendor_payload(void) {
    uint8_t probe[sizeof(kFlockIeProbe)];
    memcpy(probe, kFlockIeProbe, sizeof(probe));
    probe[13] = 0x51;   // first payload byte of the LiteON vendor IE 0x50→0x51
    TEST_ASSERT_FALSE(fyCheckFlockIeSignature(probe, sizeof(probe)));
}

// An ordinary probe request (no vendor IE at all) must not match, so the
// fingerprint is not merely "any zero-length-SSID probe".
void test_ie_sig_rejects_unrelated_probe(void) {
    const uint8_t plain[] = {
        0x00, 0x00,              // empty SSID
        0x01, 0x01, 0x82,        // tag 1
        0x32, 0x04, 0x0C, 0x12, 0x18, 0x60,   // tag 50
    };
    TEST_ASSERT_FALSE(fyCheckFlockIeSignature(plain, sizeof(plain)));
}

// Promiscuous captures commonly carry the frame's 4-byte FCS and/or a leading
// empty-SSID IE; the oversized-length ("phantom") handling must absorb them
// rather than abort the parse.
void test_ie_sig_tolerates_trailing_fcs(void) {
    uint8_t probe[sizeof(kFlockIeProbe) + 4];
    memcpy(probe, kFlockIeProbe, sizeof(kFlockIeProbe));
    probe[sizeof(kFlockIeProbe) + 0] = 0xDE;
    probe[sizeof(kFlockIeProbe) + 1] = 0xAD;
    probe[sizeof(kFlockIeProbe) + 2] = 0xBE;
    probe[sizeof(kFlockIeProbe) + 3] = 0xEF;
    TEST_ASSERT_TRUE(fyCheckFlockIeSignature(probe, sizeof(probe)));
}

// Leading empty-SSID IE present or absent must both parse to the same thing.
void test_ie_sig_leading_empty_ssid_variant(void) {
    TEST_ASSERT_TRUE(fyCheckFlockIeSignature(kFlockIeProbe + 2,
                                             sizeof(kFlockIeProbe) - 2));
}

void test_ie_sig_rejects_null_and_short_inputs(void) {
    TEST_ASSERT_FALSE(fyCheckFlockIeSignature(nullptr, 0));
    const uint8_t one[] = { 0x00 };
    TEST_ASSERT_FALSE(fyCheckFlockIeSignature(one, 1));
}

// ─────────────────────────────────────────────────────────────────────────────

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_oui_14b5cd_in_mfr_tier_only);
    RUN_TEST(test_oui_tier_counts);
    RUN_TEST(test_ble_mfr_id_table);

    RUN_TEST(test_ssid_keyword_flock_forms);
    RUN_TEST(test_ssid_keyword_test_flck_cve);
    RUN_TEST(test_ssid_keyword_negatives);
    RUN_TEST(test_every_ssid_keyword_matches_itself);

    RUN_TEST(test_ie_sig_encoding_matches_allowlist_string);
    RUN_TEST(test_ie_sig_matches_drive_tested_probe);
    RUN_TEST(test_ie_sig_rejects_wrong_vendor_payload);
    RUN_TEST(test_ie_sig_rejects_unrelated_probe);
    RUN_TEST(test_ie_sig_tolerates_trailing_fcs);
    RUN_TEST(test_ie_sig_leading_empty_ssid_variant);
    RUN_TEST(test_ie_sig_rejects_null_and_short_inputs);

    return UNITY_END();
}
