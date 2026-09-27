// fy_config.h — user configuration blob (web-flasher configurator)
//
// See docs/adr/0001-config-partition-and-flasher-configurator.md for the decision
// and the reasoning. In short: users pick which detections are active, the alert
// threshold, the BLE proximity floor and which outputs fire — in the web flasher,
// before flashing — without anyone rebuilding firmware and without pre-building
// 2^N variants.
//
// HOW IT REACHES THE DEVICE: the flasher generates these exact bytes in JS and
// writes them to a dedicated `fycfg` partition as an extra ESP Web Tools manifest
// part. No filesystem, no NVS page format — a flat struct that cannot be
// "unreadable", only valid or not.
//
// THE MOST IMPORTANT PROPERTY: absent or invalid config is NOT an error. A blank
// (erased = 0xFF) or bad-CRC partition means "no user configuration", and every
// accessor falls back to the compile-time defaults that reproduce today's
// behaviour exactly. A plain flash therefore behaves identically to a build from
// before this file existed, and a corrupt blob degrades to defaults rather than
// to something undefined.
//
// This header is deliberately free of Arduino/ESP-IDF dependencies (the caller
// reads the partition and hands the bytes in) so the whole codec is unit-tested
// on the host — see test/test_config/.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// "FYCF" — identifies a flock-you config blob. A different project must use a
// different magic so a stray blob from another tool is ignored, not misread.
#define FYCFG_MAGIC      0x46435946u
#define FYCFG_VERSION    1
#define FYCFG_TOTAL_LEN  64            // fixed; room to grow without a rewrite
#define FYCFG_CRC_OFFSET 60            // crc32 over bytes [0, 60)
#define FYCFG_MAX_ENGINE 31            // bit index limit for the engine mask

// Output enables (flags).
#define FYCFG_FLAG_LED        (1u << 0)   // status LED / screen flashes on alerts
#define FYCFG_FLAG_CHIRP      (1u << 1)   // audible alert tones
#define FYCFG_FLAG_VIBRATE    (1u << 3)   // vibration motor (M5Stack Core2 only)
// Bit 2 is RESERVED — NOT IMPLEMENTED. A "hold the alert on screen" toggle was
// scoped but is wired to no behaviour in this firmware, so the flasher does not
// offer it. It stays defined (and is accepted on decode, for layout compatibility
// with the sibling project's config) but is deliberately EXCLUDED from
// ALL_OUTPUTS so it cannot be reported as an enabled output anywhere. A switch
// that does nothing is worse than no switch — the same reasoning that removed the
// "Total events" counter and the untested 3D case.
#define FYCFG_FLAG_HOLD_RESERVED (1u << 2)
// bits 4..15 reserved
//
// Not every board has every output — the Atom Lite has no sound hardware at all,
// only the Core2 has a motor, and some boards use a screen instead of an LED. The
// flasher only offers the toggles a board can actually honour (see BOARD_CAPS in
// docs/index.html); these bits are still accepted on boards that lack the
// hardware, where they simply have no effect.
#define FYCFG_FLAG_ALL_OUTPUTS (FYCFG_FLAG_LED | FYCFG_FLAG_CHIRP | \
                                FYCFG_FLAG_VIBRATE)

// Defaults = today's behaviour. These are the single source of truth for "what
// the firmware does when the user changes nothing" — the config layer may only
// OVERRIDE them, never redefine them, so there is no second copy to drift.
#define FYCFG_DEFAULT_FLAGS      FYCFG_FLAG_ALL_OUTPUTS
#define FYCFG_DEFAULT_ENGINES    0xFFFFFFFFu   // every detection enabled
#define FYCFG_DEFAULT_CHIRP_MIN  30             // CHIRP_MIN_CONFIDENCE
#define FYCFG_DEFAULT_RSSI_FLOOR (-100)         // accepted by RSSI_MIN today

// Bounds. THE FLOOR ON chirpMinConfidence IS LOAD-BEARING, NOT COSMETIC:
// the contract-manufacturer tier scores 20 and exists specifically to be logged
// without alerting. Letting the threshold drop below 30 makes that tier chirp and
// flash, which is the exact failure this project has hit three times (mfr-tier
// wildcard probes, the IE-fingerprint bonus, and the Raven UUID range) — every
// one of them presenting as status LEDs that looked permanently stuck red,
// because re-triggering the flash outran its own expiry. Users may therefore make
// the device QUIETER, never loud-enough-to-alarm-on-shared-hardware.
#define FYCFG_MIN_CHIRP_MIN     30
#define FYCFG_MAX_CHIRP_MIN     90
#define FYCFG_MIN_RSSI_FLOOR    (-100)
#define FYCFG_MAX_RSSI_FLOOR    (-40)

typedef struct {
    uint16_t flags;        // FYCFG_FLAG_*
    uint32_t engines;      // bit per engine (see fyCfgEngineBit)
    uint8_t  chirpMin;     // minimum confidence that may alert/chirp
    int8_t   rssiFloor;    // ignore BLE detections weaker than this (dBm)
    uint8_t  reserved[2];
} FyConfig;

// The loaded configuration, defined once in main.cpp. Declared here so the
// display/UI headers can consult it without knowing where it lives — they need
// it to gate outputs (the Core2 motor, the LED, the chirp). Never written after
// setup(), which is what makes reading it from the UI task (and from the alert
// queue) safe without a lock.
//
// The native test build does not define these; it only exercises the pure codec
// functions below and never references the globals.
extern FyConfig g_cfg;
extern bool     g_cfgLoaded;

// ── CRC32 (IEEE 802.3, reflected, poly 0xEDB88320) ───────────────────────────
// Bitwise rather than table-driven: this runs once at boot, and the JS in
// docs/index.html implements the identical algorithm — keeping both short and
// obviously-matching matters more here than speed.
static inline uint32_t fyCfgCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

// ── Defaults / clamping ──────────────────────────────────────────────────────

static inline void fyCfgDefaults(FyConfig* c) {
    memset(c, 0, sizeof(*c));
    c->flags    = FYCFG_DEFAULT_FLAGS;
    c->engines  = FYCFG_DEFAULT_ENGINES;
    c->chirpMin = FYCFG_DEFAULT_CHIRP_MIN;
    c->rssiFloor = FYCFG_DEFAULT_RSSI_FLOOR;
}

static inline uint8_t fyCfgClampU8(uint8_t v, uint8_t lo, uint8_t hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}
static inline int8_t fyCfgClamp8(int v, int lo, int hi) {
    return (int8_t)((v < lo) ? lo : (v > hi) ? hi : v);
}

// Clamp every field into the range the scoring model assumes. Called on decode
// AND after a user edit, so a hand-crafted blob cannot smuggle in a value the
// firmware would otherwise trust. See the FYCFG_MIN_CHIRP_MIN note above for why
// this is not merely defensive.
static inline void fyCfgClamp(FyConfig* c) {
    c->chirpMin   = fyCfgClampU8(c->chirpMin, FYCFG_MIN_CHIRP_MIN, FYCFG_MAX_CHIRP_MIN);
    c->rssiFloor  = fyCfgClamp8((int)c->rssiFloor, FYCFG_MIN_RSSI_FLOOR, FYCFG_MAX_RSSI_FLOOR);
    c->flags     &= FYCFG_FLAG_ALL_OUTPUTS;   // drop reserved bits
    // engines: any subset is legal — that is the user's actual choice.
}

// ── Field accessors (defaults when no config is loaded) ──────────────────────

static inline bool fyCfgOutputEnabled(const FyConfig* c, bool loaded, uint16_t flag) {
    if (!loaded) return (FYCFG_DEFAULT_FLAGS & flag) != 0;
    return (c->flags & flag) != 0;
}
static inline uint32_t fyCfgEngines(const FyConfig* c, bool loaded) {
    return loaded ? c->engines : FYCFG_DEFAULT_ENGINES;
}
static inline uint8_t fyCfgChirpMin(const FyConfig* c, bool loaded) {
    return loaded ? c->chirpMin : FYCFG_DEFAULT_CHIRP_MIN;
}
static inline int8_t fyCfgRssiFloor(const FyConfig* c, bool loaded) {
    return loaded ? c->rssiFloor : FYCFG_DEFAULT_RSSI_FLOOR;
}

// ── Decode / encode ──────────────────────────────────────────────────────────

// Decode a raw partition image. Returns true when a valid blob was found; on
// ANY failure the destination is filled with defaults and true is NOT returned,
// so the caller can log "using defaults" rather than reporting an error the user
// cannot act on. Failure is expected and normal: an unflashed partition reads as
// all-0xFF, which fails the magic test.
static inline bool fyCfgDecode(const uint8_t* buf, size_t len, FyConfig* out) {
    if (!out) return false;
    fyCfgDefaults(out);
    if (!buf || len < FYCFG_TOTAL_LEN) return false;

    uint32_t magic;
    memcpy(&magic, buf + 0, 4);
    if (magic != FYCFG_MAGIC) return false;

    uint8_t version = buf[4];
    // A newer version than we understand must be ignored, never reinterpreted:
    // we cannot know what changed, and guessing would apply half a config.
    if (version == 0 || version > FYCFG_VERSION) return false;

    uint32_t stored;
    memcpy(&stored, buf + FYCFG_CRC_OFFSET, 4);
    if (stored != fyCfgCrc32(buf, FYCFG_CRC_OFFSET)) return false;

    uint16_t flags;
    uint32_t engines;
    memcpy(&flags,   buf + 6, 2);
    memcpy(&engines, buf + 8, 4);

    out->flags    = flags;
    out->engines  = engines;
    out->chirpMin = buf[12];
    out->rssiFloor = (int8_t)buf[13];
    fyCfgClamp(out);
    return true;
}

// Encode a blob into a caller-provided buffer of at least FYCFG_TOTAL_LEN bytes.
// Only used by tests and by the cross-check against the flasher's JS builder —
// the device never writes its own config, so there is no way for a device-side
// bug to corrupt the user's settings.
static inline void fyCfgEncode(const FyConfig* c, uint8_t* buf, size_t len) {
    if (!buf || len < FYCFG_TOTAL_LEN || !c) return;
    memset(buf, 0, FYCFG_TOTAL_LEN);
    uint32_t magic = FYCFG_MAGIC;
    memcpy(buf + 0, &magic, 4);
    buf[4] = FYCFG_VERSION;
    uint16_t flags = c->flags;
    uint32_t engines = c->engines;
    memcpy(buf + 6, &flags, 2);
    memcpy(buf + 8, &engines, 4);
    buf[12] = c->chirpMin;
    buf[13] = (uint8_t)c->rssiFloor;
    uint32_t crc = fyCfgCrc32(buf, FYCFG_CRC_OFFSET);
    memcpy(buf + FYCFG_CRC_OFFSET, &crc, 4);
}

