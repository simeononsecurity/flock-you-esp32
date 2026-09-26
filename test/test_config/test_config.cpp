// Unit tests for the user-configuration codec (fy_config.h).
// Runs on the host with: pio test -e native
//
// The config blob is written by the web flasher (JavaScript) and read by the
// firmware (C), so these tests are the contract between two languages. Two
// things matter more than the happy path:
//
//   1. Absent or invalid config MUST fall back to defaults, because that is what
//      a plain flash produces and what the compile-time behaviour already is. A
//      device that refuses to start (or applies half a config) because of a
//      missing blob would be a worse bug than any misconfiguration.
//   2. The CRC must be the *standard* IEEE CRC-32, because the JS side computes
//      it independently. A "close enough" implementation would reject every real
//      blob, and the symptom would look like "my settings did nothing".

#include <unity.h>
#include <string.h>
#include "../../fy_config.h"

static uint8_t blob[FYCFG_TOTAL_LEN];

// ── defaults are exactly today's behaviour ───────────────────────────────────

void test_defaults_match_current_behaviour(void) {
    FyConfig c; fyCfgDefaults(&c);
    TEST_ASSERT_EQUAL_UINT16(FYCFG_FLAG_ALL_OUTPUTS, c.flags);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, c.engines);
    TEST_ASSERT_EQUAL_UINT8(30, c.chirpMin);
    TEST_ASSERT_EQUAL_INT8(-100, c.rssiFloor);
}

// With no blob loaded every accessor must report the default, so callers can ask
// unconditionally instead of scattering `if (loaded)` at each use site.
void test_accessors_fall_back_when_not_loaded(void) {
    FyConfig c; fyCfgDefaults(&c);
    c.flags = 0; c.engines = 0; c.chirpMin = 90; c.rssiFloor = -40;  // a hostile struct
    TEST_ASSERT_TRUE(fyCfgOutputEnabled(&c, false, FYCFG_FLAG_CHIRP));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, fyCfgEngines(&c, false));
    TEST_ASSERT_EQUAL_UINT8(30, fyCfgChirpMin(&c, false));
    TEST_ASSERT_EQUAL_INT8(-100, fyCfgRssiFloor(&c, false));
}

// ── the CRC must be the standard one, or the JS side cannot agree ────────────

void test_crc32_matches_standard_check_value(void) {
    const char* s = "123456789";
    TEST_ASSERT_EQUAL_UINT32(0xCBF43926u, fyCfgCrc32((const uint8_t*)s, 9));
}

void test_crc32_empty_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0x00000000u, fyCfgCrc32((const uint8_t*)"", 0));
}

// ── round trip ───────────────────────────────────────────────────────────────

void test_round_trip(void) {
    FyConfig in; fyCfgDefaults(&in);
    in.flags    = FYCFG_FLAG_CHIRP;              // LED and HOLD off
    in.engines  = 0x00000021u;                   // bits 0 and 5 only
    in.chirpMin = 55;
    in.rssiFloor = -80;
    fyCfgEncode(&in, blob, sizeof(blob));

    FyConfig out;
    TEST_ASSERT_TRUE(fyCfgDecode(blob, sizeof(blob), &out));
    TEST_ASSERT_EQUAL_UINT16(in.flags, out.flags);
    TEST_ASSERT_EQUAL_UINT32(in.engines, out.engines);
    TEST_ASSERT_EQUAL_UINT8(in.chirpMin, out.chirpMin);
    TEST_ASSERT_EQUAL_INT8(in.rssiFloor, out.rssiFloor);
}

// The flasher's JS is written against these byte offsets, so they are part of
// the interface and not an implementation detail.
void test_layout_offsets(void) {
    FyConfig in; fyCfgDefaults(&in);
    in.chirpMin = 42; in.rssiFloor = -77; in.flags = FYCFG_FLAG_LED;
    fyCfgEncode(&in, blob, sizeof(blob));

    uint32_t magic; memcpy(&magic, blob + 0, 4);
    uint16_t flags; memcpy(&flags, blob + 6, 2);
    TEST_ASSERT_EQUAL_HEX32(FYCFG_MAGIC, magic);
    TEST_ASSERT_EQUAL_UINT8(FYCFG_VERSION, blob[4]);
    TEST_ASSERT_EQUAL_HEX16(FYCFG_FLAG_LED, flags);
    TEST_ASSERT_EQUAL_UINT8(42, blob[12]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(int8_t)-77, blob[13]);
    TEST_ASSERT_EQUAL_UINT8(0, blob[14]);   // reserved stays zero
}

// ── absent / invalid config falls back to defaults ───────────────────────────

// A freshly erased partition reads as all-0xFF, which is what every device that
// was flashed without the configurator will have.
void test_blank_partition_is_invalid_and_defaults(void) {
    memset(blob, 0xFF, sizeof(blob));
    FyConfig out;
    TEST_ASSERT_FALSE(fyCfgDecode(blob, sizeof(blob), &out));
    TEST_ASSERT_EQUAL_UINT8(FYCFG_DEFAULT_CHIRP_MIN, out.chirpMin);
    TEST_ASSERT_EQUAL_UINT32(FYCFG_DEFAULT_ENGINES, out.engines);
    TEST_ASSERT_EQUAL_UINT16(FYCFG_DEFAULT_FLAGS, out.flags);
}

void test_wrong_magic_is_rejected(void) {
    FyConfig in; fyCfgDefaults(&in);
    fyCfgEncode(&in, blob, sizeof(blob));
    blob[0] ^= 0xFF;   // corrupt the magic only
    FyConfig out;
    TEST_ASSERT_FALSE(fyCfgDecode(blob, sizeof(blob), &out));
}

// Any single-byte corruption in the covered region must be caught — this is the
// power-loss-mid-write case the CRC exists for.
void test_corruption_anywhere_is_caught(void) {
    FyConfig in; fyCfgDefaults(&in);
    in.engines = 0x0F0F0F0Fu;
    for (int i = 0; i < FYCFG_CRC_OFFSET; i++) {
        fyCfgEncode(&in, blob, sizeof(blob));
        blob[i] ^= 0x01;
        FyConfig out;
        TEST_ASSERT_FALSE_MESSAGE(fyCfgDecode(blob, sizeof(blob), &out),
                                  "single-byte corruption must fail the CRC");
    }
}

void test_truncated_and_null_input_are_safe(void) {
    FyConfig in; fyCfgDefaults(&in);
    fyCfgEncode(&in, blob, sizeof(blob));
    FyConfig out;
    TEST_ASSERT_FALSE(fyCfgDecode(blob, FYCFG_TOTAL_LEN - 1, &out));
    TEST_ASSERT_FALSE(fyCfgDecode(NULL, sizeof(blob), &out));
}

// A blob from a future firmware version must be ignored, not reinterpreted: we
// cannot know what changed, and applying half of it would be worse than none.
void test_future_version_is_ignored(void) {
    FyConfig in; fyCfgDefaults(&in);
    fyCfgEncode(&in, blob, sizeof(blob));
    blob[4] = FYCFG_VERSION + 1;
    uint32_t crc = fyCfgCrc32(blob, FYCFG_CRC_OFFSET);
    memcpy(blob + FYCFG_CRC_OFFSET, &crc, 4);   // keep the CRC valid
    FyConfig out;
    TEST_ASSERT_FALSE(fyCfgDecode(blob, sizeof(blob), &out));
    TEST_ASSERT_EQUAL_UINT8(FYCFG_DEFAULT_CHIRP_MIN, out.chirpMin);
}

void test_version_zero_is_ignored(void) {
    FyConfig in; fyCfgDefaults(&in);
    fyCfgEncode(&in, blob, sizeof(blob));
    blob[4] = 0;
    uint32_t crc = fyCfgCrc32(blob, FYCFG_CRC_OFFSET);
    memcpy(blob + FYCFG_CRC_OFFSET, &crc, 4);
    FyConfig out;
    TEST_ASSERT_FALSE(fyCfgDecode(blob, sizeof(blob), &out));
}


// ── clamping: the load-bearing part ──────────────────────────────────────────

// A threshold below 30 would let the contract-manufacturer tier (score 20) chirp
// and flash — the stuck-red-LED failure this project has hit three times. The
// floor is therefore enforced in the firmware, not just hidden in the UI.
void test_chirp_threshold_floor_is_enforced(void) {
    FyConfig c; fyCfgDefaults(&c);
    c.chirpMin = 5;
    fyCfgClamp(&c);
    TEST_ASSERT_EQUAL_UINT8(FYCFG_MIN_CHIRP_MIN, c.chirpMin);

    c.chirpMin = 0;
    fyCfgClamp(&c);
    TEST_ASSERT_EQUAL_UINT8(FYCFG_MIN_CHIRP_MIN, c.chirpMin);

    // ...and also through the decode path, so a hand-built blob cannot bypass it.
    FyConfig in; fyCfgDefaults(&in);
    in.chirpMin = 0;
    fyCfgEncode(&in, blob, sizeof(blob));
    FyConfig out;
    TEST_ASSERT_TRUE(fyCfgDecode(blob, sizeof(blob), &out));
    TEST_ASSERT_EQUAL_UINT8(FYCFG_MIN_CHIRP_MIN, out.chirpMin);
}

void test_chirp_threshold_ceiling_is_enforced(void) {
    FyConfig c; fyCfgDefaults(&c);
    c.chirpMin = 250;
    fyCfgClamp(&c);
    TEST_ASSERT_EQUAL_UINT8(FYCFG_MAX_CHIRP_MIN, c.chirpMin);
}

void test_rssi_floor_is_clamped_both_ways(void) {
    FyConfig c; fyCfgDefaults(&c);
    c.rssiFloor = (int8_t)-128;
    fyCfgClamp(&c);
    TEST_ASSERT_EQUAL_INT8(FYCFG_MIN_RSSI_FLOOR, c.rssiFloor);
    c.rssiFloor = (int8_t)10;      // nonsense: stronger than any real signal
    fyCfgClamp(&c);
    TEST_ASSERT_EQUAL_INT8(FYCFG_MAX_RSSI_FLOOR, c.rssiFloor);
    c.rssiFloor = (int8_t)-85;     // legal value survives untouched
    fyCfgClamp(&c);
    TEST_ASSERT_EQUAL_INT8(-85, c.rssiFloor);
}

// Reserved flag bits written by a future/other tool must be dropped rather than
// carried around and accidentally interpreted later.
void test_reserved_flag_bits_are_dropped(void) {
    FyConfig c; fyCfgDefaults(&c);
    c.flags = 0xFFFF;
    fyCfgClamp(&c);
    TEST_ASSERT_EQUAL_UINT16(FYCFG_FLAG_ALL_OUTPUTS, c.flags);
}

// Disabling every detection is a legal configuration, and must survive
// encode/decode rather than being "helpfully" corrected to all-on.
void test_engine_mask_may_be_empty_or_partial(void) {
    FyConfig in; fyCfgDefaults(&in);
    in.engines = 0;
    fyCfgEncode(&in, blob, sizeof(blob));
    FyConfig out;
    TEST_ASSERT_TRUE(fyCfgDecode(blob, sizeof(blob), &out));
    TEST_ASSERT_EQUAL_UINT32(0, out.engines);
    TEST_ASSERT_EQUAL_UINT32(0, fyCfgEngines(&out, true));

    in.engines = 1u << FYCFG_MAX_ENGINE;   // highest addressable bit
    fyCfgEncode(&in, blob, sizeof(blob));
    TEST_ASSERT_TRUE(fyCfgDecode(blob, sizeof(blob), &out));
    TEST_ASSERT_EQUAL_UINT32(1u << FYCFG_MAX_ENGINE, out.engines);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_defaults_match_current_behaviour);
    RUN_TEST(test_accessors_fall_back_when_not_loaded);
    RUN_TEST(test_crc32_matches_standard_check_value);
    RUN_TEST(test_crc32_empty_is_zero);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_layout_offsets);
    RUN_TEST(test_blank_partition_is_invalid_and_defaults);
    RUN_TEST(test_wrong_magic_is_rejected);
    RUN_TEST(test_corruption_anywhere_is_caught);
    RUN_TEST(test_truncated_and_null_input_are_safe);
    RUN_TEST(test_future_version_is_ignored);
    RUN_TEST(test_version_zero_is_ignored);
    RUN_TEST(test_chirp_threshold_floor_is_enforced);
    RUN_TEST(test_chirp_threshold_ceiling_is_enforced);
    RUN_TEST(test_rssi_floor_is_clamped_both_ways);
    RUN_TEST(test_reserved_flag_bits_are_dropped);
    RUN_TEST(test_engine_mask_may_be_empty_or_partial);

    return UNITY_END();
}

