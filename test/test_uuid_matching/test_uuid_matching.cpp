// Unit tests for Raven UUID matching and firmware version estimation.
// Runs on the host with: pio test -e native
// No hardware or ESP32 toolchain required.

#include <unity.h>
#include "../../fy_detect.h"

// ── Raven match CLASSIFICATION (named vs merely in-range) ────────────────────
// These two are scored very differently: a named, GainSec-documented service may
// alert stand-alone, while an unnamed value that merely sits inside 0x3100-0x3500
// is recorded below the chirp threshold. Splitting them was driven by a live
// false positive: an unnamed device with a randomised MAC at -88 dBm chirped and
// held the alert LED red purely for being "in range".

void test_raven_classify_named(void) {
    const char* gps[] = { "00003100-0000-1000-8000-00805f9b34fb" };
    TEST_ASSERT_EQUAL_INT(FY_RAVEN_MATCH_NAMED,
        fyClassifyRavenUUIDFromStrings(gps, 1, nullptr));
    // The GPS-leaking services are inside the block but are NOT in the named
    // table, so they classify as RANGE — deliberately, since a bare in-range
    // value is what produced the false positive.
    const char* u3101[] = { "00003101-0000-1000-8000-00805f9b34fb" };
    TEST_ASSERT_EQUAL_INT(FY_RAVEN_MATCH_RANGE,
        fyClassifyRavenUUIDFromStrings(u3101, 1, nullptr));
}

void test_raven_classify_range_and_none(void) {
    const char* rng[]  = { "0x3110" };                      // in range, unnamed
    TEST_ASSERT_EQUAL_INT(FY_RAVEN_MATCH_RANGE,
        fyClassifyRavenUUIDFromStrings(rng, 1, nullptr));
    const char* none[] = { "0x4100" };                      // outside the block
    TEST_ASSERT_EQUAL_INT(FY_RAVEN_MATCH_NONE,
        fyClassifyRavenUUIDFromStrings(none, 1, nullptr));
}

// A named service anywhere in the list must win over an in-range one, whichever
// order they are advertised in — otherwise a device offering both would be
// under-scored depending on NimBLE's enumeration order.
void test_raven_classify_named_beats_range(void) {
    const char* rangeFirst[] = {
        "00003110-0000-1000-8000-00805f9b34fb",
        "00003200-0000-1000-8000-00805f9b34fb",
    };
    TEST_ASSERT_EQUAL_INT(FY_RAVEN_MATCH_NAMED,
        fyClassifyRavenUUIDFromStrings(rangeFirst, 2, nullptr));
    const char* namedFirst[] = {
        "00003200-0000-1000-8000-00805f9b34fb",
        "00003110-0000-1000-8000-00805f9b34fb",
    };
    TEST_ASSERT_EQUAL_INT(FY_RAVEN_MATCH_NAMED,
        fyClassifyRavenUUIDFromStrings(namedFirst, 2, nullptr));
}

// The boolean wrapper must keep its old meaning (ANY kind of Raven match), since
// other callers still rely on it.
void test_raven_wrapper_still_true_for_range(void) {
    const char* rng[] = { "0x3110" };
    TEST_ASSERT_TRUE(fyCheckRavenUUIDFromStrings(rng, 1, nullptr));
    const char* none[] = { "0x4100" };
    TEST_ASSERT_FALSE(fyCheckRavenUUIDFromStrings(none, 1, nullptr));
}

// ── Raven UUID matching tests ─────────────────────────────────────────────────

void test_raven_uuid_known_vendor_service(void) {
    const char* uuids[] = { FY_RAVEN_GPS };
    char out[41] = {0};
    TEST_ASSERT_TRUE(fyCheckRavenUUIDFromStrings(uuids, 1, out));
    TEST_ASSERT_EQUAL_STRING(FY_RAVEN_GPS, out);
}

// BEHAVIOUR CHANGE (deliberate): 0x180A / 0x1809 / 0x1819 are *standard
// Bluetooth SIG* services, present on essentially every BLE device ever made
// (phones, watches, earbuds, fitness bands). They used to sit in
// fy_raven_uuids[], scoring CS_BLE_UUID_STANDALONE=45 (> chirp threshold), so
// any passing fitness tracker alerted as a "Raven camera". They must no longer
// match standalone. This test previously asserted the 0x180A match positively.
void test_raven_uuid_standard_services_never_alert(void) {
    const char* std_uuids[] = {
        "0000180a-0000-1000-8000-00805f9b34fb",  // Device Information
        "00001809-0000-1000-8000-00805f9b34fb",  // Health Thermometer
        "00001819-0000-1000-8000-00805f9b34fb",  // Location and Navigation
    };
    for (size_t i = 0; i < 3; i++) {
        const char* one[] = { std_uuids[i] };
        TEST_ASSERT_FALSE_MESSAGE(
            fyCheckRavenUUIDFromStrings(one, 1, nullptr), std_uuids[i]);
    }
}

void test_standard_service_classifier(void) {
    TEST_ASSERT_TRUE(fyService16IsStandardSvc(0x1800));   // Generic Access
    TEST_ASSERT_TRUE(fyService16IsStandardSvc(0x1809));
    TEST_ASSERT_TRUE(fyService16IsStandardSvc(0x180A));
    TEST_ASSERT_TRUE(fyService16IsStandardSvc(0x1819));
    // Raven's vendor range is not a SIG assignment.
    TEST_ASSERT_FALSE(fyService16IsStandardSvc(0x3100));
    TEST_ASSERT_FALSE(fyService16IsStandardSvc(0x3101));
    TEST_ASSERT_FALSE(fyService16IsStandardSvc(0x3500));
}

// Regression guard: no entry in the Raven alert table may be a standard
// service, so re-adding one fails here rather than on a user's wrist.
void test_raven_table_has_no_standard_services(void) {
    for (size_t i = 0; i < FY_RAVEN_UUID_COUNT; i++) {
        int svc = fyService16FromUuidString(fy_raven_uuids[i]);
        TEST_ASSERT_FALSE_MESSAGE(
            svc >= 0 && fyService16IsStandardSvc((uint16_t)svc),
            fy_raven_uuids[i]);
    }
}

void test_raven_uuid_case_insensitive(void) {
    // Upper-case form of a *vendor* service must still match (this test used to
    // use 0x180A, which now correctly does not alert).
    const char* uuids[] = { "00003100-0000-1000-8000-00805F9B34FB" };
    TEST_ASSERT_TRUE(fyCheckRavenUUIDFromStrings(uuids, 1, nullptr));
}

void test_raven_uuid_all_known(void) {
    for (size_t i = 0; i < FY_RAVEN_UUID_COUNT; i++) {
        const char* uuids[] = { fy_raven_uuids[i] };
        TEST_ASSERT_TRUE_MESSAGE(
            fyCheckRavenUUIDFromStrings(uuids, 1, nullptr),
            fy_raven_uuids[i]);
    }
}

void test_raven_uuid_no_match(void) {
    const char* uuids[] = { "12345678-1234-1234-1234-123456789abc" };
    TEST_ASSERT_FALSE(fyCheckRavenUUIDFromStrings(uuids, 1, nullptr));
}

void test_raven_uuid_null_list(void) {
    TEST_ASSERT_FALSE(fyCheckRavenUUIDFromStrings(nullptr, 0, nullptr));
}

void test_raven_uuid_empty_list(void) {
    const char* uuids[] = { nullptr };
    TEST_ASSERT_FALSE(fyCheckRavenUUIDFromStrings(uuids, 0, nullptr));
}

void test_raven_uuid_mixed_list_finds_known(void) {
    // First entry unknown, second known — should still match
    const char* uuids[] = {
        "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
        FY_RAVEN_GPS
    };
    char out[41] = {0};
    TEST_ASSERT_TRUE(fyCheckRavenUUIDFromStrings(uuids, 2, out));
    TEST_ASSERT_EQUAL_STRING(FY_RAVEN_GPS, out);
}

void test_raven_uuid_null_out_buffer_ok(void) {
    const char* uuids[] = { FY_RAVEN_POWER };
    // Passing nullptr for out_uuid must not crash
    TEST_ASSERT_TRUE(fyCheckRavenUUIDFromStrings(uuids, 1, nullptr));
}

void test_raven_old_style_short_uuids_do_not_match(void) {
    // Verify the old short-form UUIDs (used in pre-PR#39 firmware) do NOT
    // match the full 128-bit UUID strings — they are different representations.
    const char* old_short[] = {
        "1b7e",  // was RAVEN_SVC_UUID_PRIMARY
        "fd60"   // was RAVEN_SVC_UUID_TELEM
    };
    TEST_ASSERT_FALSE(fyCheckRavenUUIDFromStrings(old_short, 2, nullptr));
}

// ── Raven service RANGE tests (0x3100-0x3500) ────────────────────────────────
//
// The named table only holds the round hundred values. The camera advertises
// services across the whole 0x3100-0x3500 range, and 0x3101/0x3102 — the
// unauthenticated ones that leak GPS lat/long — are precisely NOT in the table.
// The range check is what catches them.

void test_raven_range_gps_leaking_services(void) {
    const char* u3101[] = { "00003101-0000-1000-8000-00805f9b34fb" };
    const char* u3102[] = { "00003102-0000-1000-8000-00805f9b34fb" };
    TEST_ASSERT_TRUE(fyCheckRavenUUIDFromStrings(u3101, 1, nullptr));
    TEST_ASSERT_TRUE(fyCheckRavenUUIDFromStrings(u3102, 1, nullptr));
}

void test_raven_range_short_form(void) {
    const char* u[] = { "0x3110" };
    TEST_ASSERT_TRUE(fyCheckRavenUUIDFromStrings(u, 1, nullptr));
}

void test_raven_range_bounds(void) {
    TEST_ASSERT_TRUE(fyCheckRavenServiceRange(0x3100));
    TEST_ASSERT_TRUE(fyCheckRavenServiceRange(0x3500));
    TEST_ASSERT_FALSE(fyCheckRavenServiceRange(0x30ff));
    TEST_ASSERT_FALSE(fyCheckRavenServiceRange(0x3501));
}

void test_raven_range_rejects_out_of_range(void) {
    const char* low[]  = { "000030ff-0000-1000-8000-00805f9b34fb" };
    const char* high[] = { "00003600-0000-1000-8000-00805f9b34fb" };
    TEST_ASSERT_FALSE(fyCheckRavenUUIDFromStrings(low, 1, nullptr));
    TEST_ASSERT_FALSE(fyCheckRavenUUIDFromStrings(high, 1, nullptr));
}

void test_service16_parser(void) {
    TEST_ASSERT_EQUAL(0x3101, fyService16FromUuidString("00003101-0000-1000-8000-00805f9b34fb"));
    TEST_ASSERT_EQUAL(0x3101, fyService16FromUuidString("3101"));
    TEST_ASSERT_EQUAL(0x3101, fyService16FromUuidString("0x3101"));
    // A genuine 128-bit vendor UUID is not a Bluetooth-base-derived service.
    TEST_ASSERT_EQUAL(-1, fyService16FromUuidString("e8ccbb38-9532-46a8-9fe5-1814df172e6f"));
    TEST_ASSERT_EQUAL(-1, fyService16FromUuidString("zzzz"));
    TEST_ASSERT_EQUAL(-1, fyService16FromUuidString(nullptr));
}

// ── Raven firmware version estimation tests ───────────────────────────────────

void test_fw_v11x(void) {
    // Old location service only — firmware 1.1.x
    TEST_ASSERT_EQUAL_STRING("1.1.x", fyEstimateRavenFW(false, true, false));
}

void test_fw_v12x(void) {
    // New GPS present, no power service — firmware 1.2.x
    TEST_ASSERT_EQUAL_STRING("1.2.x", fyEstimateRavenFW(true, false, false));
}

void test_fw_v13x(void) {
    // New GPS + power present — firmware 1.3.x
    TEST_ASSERT_EQUAL_STRING("1.3.x", fyEstimateRavenFW(true, false, true));
}

void test_fw_v13x_all_flags(void) {
    // All flags set — new GPS+power wins over old location
    TEST_ASSERT_EQUAL_STRING("1.3.x", fyEstimateRavenFW(true, true, true));
}

void test_fw_unknown(void) {
    // No identifying service categories
    TEST_ASSERT_EQUAL_STRING("?", fyEstimateRavenFW(false, false, false));
}

void test_fw_only_old_health(void) {
    // Only old health service — no GPS or power — unknown version
    TEST_ASSERT_EQUAL_STRING("?", fyEstimateRavenFW(false, false, false));
}

// ── UUID count sanity ─────────────────────────────────────────────────────────

void test_raven_uuid_count(void) {
    // 5 VENDOR-SPECIFIC services may alert standalone. The 3 legacy
    // standard-SIG assignments (0x180A / 0x1809 / 0x1819) moved to
    // fy_raven_legacy_uuids[] as estimation-only evidence, so they are
    // deliberately not counted here — see the standard-services block above
    // fy_raven_uuids[] for why.
    TEST_ASSERT_EQUAL(5u, (unsigned)FY_RAVEN_UUID_COUNT);
    TEST_ASSERT_EQUAL(3u, (unsigned)FY_RAVEN_LEGACY_UUID_COUNT);
}

// ─────────────────────────────────────────────────────────────────────────────

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_raven_uuid_known_vendor_service);
    RUN_TEST(test_raven_uuid_standard_services_never_alert);
    RUN_TEST(test_standard_service_classifier);
    RUN_TEST(test_raven_table_has_no_standard_services);
    RUN_TEST(test_raven_uuid_all_known);
    RUN_TEST(test_raven_uuid_case_insensitive);
    RUN_TEST(test_raven_uuid_no_match);
    RUN_TEST(test_raven_uuid_null_list);
    RUN_TEST(test_raven_uuid_empty_list);
    RUN_TEST(test_raven_uuid_mixed_list_finds_known);
    RUN_TEST(test_raven_uuid_null_out_buffer_ok);
    RUN_TEST(test_raven_old_style_short_uuids_do_not_match);

    RUN_TEST(test_raven_range_gps_leaking_services);
    RUN_TEST(test_raven_classify_named);
    RUN_TEST(test_raven_classify_range_and_none);
    RUN_TEST(test_raven_classify_named_beats_range);
    RUN_TEST(test_raven_wrapper_still_true_for_range);
    RUN_TEST(test_raven_range_short_form);
    RUN_TEST(test_raven_range_bounds);
    RUN_TEST(test_raven_range_rejects_out_of_range);
    RUN_TEST(test_service16_parser);

    RUN_TEST(test_fw_v11x);
    RUN_TEST(test_fw_v12x);
    RUN_TEST(test_fw_v13x);
    RUN_TEST(test_fw_v13x_all_flags);
    RUN_TEST(test_fw_unknown);
    RUN_TEST(test_fw_only_old_health);

    RUN_TEST(test_raven_uuid_count);

    return UNITY_END();
}
