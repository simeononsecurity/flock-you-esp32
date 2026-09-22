// fy_detect.h — Detection pattern data and pure matching logic for flock-you-esp32
//
// This header is shared between the firmware (main.cpp, compiled with the ESP32
// Arduino toolchain) and the native unit-test build (pio test -e native, compiled
// with the host GCC/Clang).  It must not include any Arduino or ESP-IDF headers.
//
// Sources:
//   OUI lists   — @NitekryDPaul (original 30), Michael/DeFlockJoplin (82:6b:f2),
//                 dougborg/PR#39 (b4:1e:52 direct reg, FS Ext Battery, e0:0a:f6 mfr)
//   BLE mfr-ID  — wgreenberg/flock-you (0x09C8 XUNTONG)
//   Raven UUIDs — GainSec research (5 ***named*** 128-bit service UUIDs that may
//                 alert stand-alone; the wider 0x3100-0x3500 block is matched
//                 separately and scored below the chirp threshold — see
//                 fyClassifyRavenUUIDFromStrings())
//   SoundThinking — avenstewart/PR#39 (d4:11:d6 / formerly ShotSpotter)
//   Firmware-extracted set — colonelpanichacks/flock-you upstream, from a Flock
//                 Safety ALPR camera firmware dump (Qualcomm MSM8953 + QCA9377
//                 radio, Android 8.1, codename "hpnotiq", extracted 2026-09-16).
//                 Adds: QCA9377 default radio MACs (see FY_EXACT_MAC_*), the
//                 Flock accessory + Nordic DFU GATT services, the DfuTarg /
//                 bare-serial BLE naming forms, and the "FS Ext Battery" SSID
//                 keyword. Provenance: datasets/firmware_derived_signatures.md.

#ifndef FY_DETECT_H
#define FY_DETECT_H

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#ifndef ARDUINO
  // POSIX host build (pio test -e native) — need strcasecmp / strncasecmp
  #ifndef _GNU_SOURCE
    #define _GNU_SOURCE
  #endif
  #include <strings.h>   // strncasecmp, strcasecmp
  #include <stdio.h>
#endif

// ============================================================================
// OUI TABLE A — HIGH-CONFIDENCE Flock Safety
// ============================================================================
// These OUIs are either directly registered to Flock Safety or have been
// exclusively observed on confirmed Flock ALPR hardware through field testing.
// An OUI-A match alone warrants a loud alert and a confidence score of ≥40.

static const char* fy_oui_high[] = {
  // Flock WiFi cameras — @NitekryDPaul promiscuous-mode dataset (30 OUIs)
  "70:c9:4e", "3c:91:80", "d8:f3:bc", "80:30:49", "b8:35:32",
  "14:5a:fc", "74:4c:a1", "08:3a:88", "9c:2f:9d", "c0:35:32",
  "94:08:53", "e4:aa:ea", "24:b2:b9",
  "b8:1e:a4", "70:08:94", "58:8e:81", "ec:1b:bd", "3c:71:bf",
  "58:00:e3", "90:35:ea", "5c:93:a2", "64:6e:69", "48:27:ea",
  "a4:cf:12", "e0:4f:43",
  // DeFlockJoplin — 12th camera found via wildcard-probe field test (PR#39)
  "82:6b:f2",
  // Flock Safety direct IEEE assignment (dougborg/PR#39)
  "b4:1e:52",
  // FS Ext Battery device series (dougborg/PR#39)
  "04:0d:84", "f0:82:c0", "1c:34:f1", "38:5b:44", "94:34:69", "b4:e3:f9"
};
#define FY_OUI_HIGH_COUNT (sizeof(fy_oui_high)/sizeof(fy_oui_high[0]))

// ============================================================================
// OUI TABLE B — CONTRACT-MANUFACTURER OUIs  (Liteon Technology / USI)
// ============================================================================
// These OUIs belong to contract manufacturers that produce Flock hardware but
// ALSO ship unrelated consumer and enterprise devices.  An OUI-B match alone
// is LOW confidence (~20 pts); do not chirp or flash without a corroborating
// signal (SSID, BLE correlation, wildcard probe, or sequential-MAC pair).

static const char* fy_oui_mfr[] = {
  "f4:6a:dd",   // Liteon Technology
  "f8:a2:d6",   // Liteon Technology
  // 14:b5:cd is Liteon too (per the IEEE lookup in oui.txt), so it belongs in
  // THIS table rather than fy_oui_high[]: Liteon hardware is shared with
  // unrelated consumer products, which is exactly why f4:6a:dd / f8:a2:d6 sit
  // here. Upstream's flat single-list model has no tier to choose, so copying
  // its classification would place a shared contract-manufacturer chipset
  // prefix at high confidence — the f8:a2:d6 false-positive class that this
  // split exists to prevent. It was missing from both tables entirely before
  // this (the community dataset has 32 prefixes; we carried 31).
  "14:b5:cd",   // Liteon Technology (community dataset sync)
  "00:f4:8d",   // Universal Scientific Industrial (USI)
  "d0:39:57",   // USI
  "e8:d0:fc",   // USI
  "e0:0a:f6",   // USI (added dougborg/PR#39)
  // Qualcomm Atheros — the QCA9377 is the radio in Flock's MSM8953-generation
  // cameras (firmware dump, 2026-09-16). It is deliberately kept in THIS low
  // tier rather than the high-confidence table: unlike b4:1e:52, 00:03:7f is a
  // chipset vendor's OUI present on a huge installed base of unrelated
  // Atheros-based gear, so on its own it is NOT evidence of a camera. It
  // matters here because the camera ships with firmware-default MACs from this
  // block before OTA provisioning assigns a Flock OUI — and those two exact
  // default MACs are checked separately, at high confidence, by
  // fyCheckFlockExactMAC() (see FY_EXACT_MAC_* below).
  "00:03:7f"    // Qualcomm Atheros QCA9377 (firmware dump, 2026-09-16)
};
#define FY_OUI_MFR_COUNT (sizeof(fy_oui_mfr)/sizeof(fy_oui_mfr[0]))

// ============================================================================
// EXACT (FULL 6-BYTE) MACS — firmware-default radio addresses
// ============================================================================
// These are the *factory default* QCA9377 radio MACs baked into the Flock
// camera firmware image (Qualcomm MSM8953 + QCA9377 dump, 2026-09-16):
//   00:03:7f:50:00:01 — bdwlan30.bin / fakeboar.bin  (WLAN NVRAM)
//   00:03:7f:4f:00:16 — otp30.bin                    (OTP / factory partition)
//
// WHY THIS IS ITS OWN, HIGH-CONFIDENCE CHECK rather than just relying on the
// 00:03:7f OUI entry above: the *OUI* is shared with every other Atheros
// device on earth, but these two *complete* addresses are not — they are what
// the radio transmits when the unit is still unprovisioned, i.e. before Flock's
// provisioning step rewrites the MAC. A device observed transmitting from an
// exact factory-default address is therefore a very specific signature, and
// (per the upstream firmware-derived set) is worth hearing loudly, whereas a
// bare 00:03:7f match is worth only a silent low-tier log.
//
// Consequence worth knowing: a *provisioned* camera will have had its MAC
// rewritten away from these values and will instead match on the assigned
// OUI — so this path only ever sees freshly-imaged/never-provisioned units.

static const char* fy_exact_macs[] = {
  "00:03:7f:50:00:01",   // bdwlan30.bin / fakeboar.bin default
  "00:03:7f:4f:00:16"    // otp30.bin default
};
#define FY_EXACT_MAC_COUNT (sizeof(fy_exact_macs)/sizeof(fy_exact_macs[0]))

// ============================================================================
// OUI TABLE C — SOUNDTHINKING / SHOTSPOTTER
// ============================================================================
// SoundThinking (formerly ShotSpotter) manufactures acoustic gunshot-detection
// sensors that are frequently co-deployed with Flock ALPR systems.  Detecting
// one indicates a surveillance-capable installation; method = "soundthinking".

static const char* fy_oui_soundthinking[] = {
  "d4:11:d6"
};
#define FY_OUI_ST_COUNT (sizeof(fy_oui_soundthinking)/sizeof(fy_oui_soundthinking[0]))

// ============================================================================
// WIFI SSID KEYWORD PATTERNS  (case-insensitive substring match)
// ============================================================================
// SINGLE SOURCE OF TRUTH: main.cpp deliberately does NOT keep its own copy of
// this list any more (it used to, which is how it drifted — the same problem
// the BLE name list below has a warning about).
//
//   "flock"          → bare deployed cameras + provisioning "Flock-XXXXXX"
//                      SoftAP, and (as a substring) "Flock Camera net."
//   "flocksafety"    → variant brand string sometimes advertised
//   "penguin"        → Flock's internal Penguin product codename (battery pack)
//   "pigvision"      → PigVision / Raven variant
//   "fs ext battery" → "FS Ext Battery" pack SoftAP (firmware dump, 2026-09-16)
//   "flck"           → CVE-2025-59409: Flock's Falcon/Sparrow LPR firmware
//                      (OPM1.171019.026) ships *development* Wi-Fi credentials
//                      ("test_flck") in cleartext in production firmware
//                      (GainSec; GHSA-7m7v-cj32-7j8q). "test_flck" does NOT
//                      match the "flock" keyword above — f-l-c-k vs f-l-o-c-k —
//                      so the truncated form needs its own entry or a camera
//                      advertising it is invisible. Only 4 chars, but "flck"
//                      is not a substring of any ordinary English word or
//                      common SSID, so it stays specific in practice.

static const char* fy_ssid_keywords[] = {
  "flock",
  "flocksafety",
  "penguin",
  "pigvision",
  "fs ext battery",
  "flck",
  nullptr
};
// Keyword count excluding the nullptr terminator (for the startup log).
#define FY_SSID_KEYWORD_COUNT \
  (sizeof(fy_ssid_keywords)/sizeof(fy_ssid_keywords[0]) - 1)

// Locale-independent ASCII lowercase. Deliberately not tolower(): that is
// locale-sensitive and this header is compiled for both the host test build and
// the ESP32 target.
static inline char fyLowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Case-insensitive substring search. Self-contained rather than POSIX
// strcasestr()/strstr(), matching how main.cpp's own strcasestr_local() behaves
// — this header must not depend on a GNU extension being available.
static inline bool fySubstrCI(const char* hay, const char* needle) {
  if (!hay || !needle || !needle[0]) return false;
  for (const char* h = hay; *h; ++h) {
    const char* hp = h;
    const char* np = needle;
    while (*hp && *np && fyLowerAscii(*hp) == fyLowerAscii(*np)) { ++hp; ++np; }
    if (!*np) return true;
  }
  return false;
}

// True when an SSID contains any Flock keyword. Single entry point for the
// firmware, the beacon tester and the native tests.
static inline bool fyCheckFlockSsidKeyword(const char* ssid) {
  if (!ssid || !ssid[0]) return false;
  for (size_t i = 0; fy_ssid_keywords[i]; i++) {
    if (fySubstrCI(ssid, fy_ssid_keywords[i])) return true;
  }
  return false;
}

// ============================================================================
// BLE DEVICE NAME PATTERNS  (case-insensitive substring match)
// ============================================================================
// SINGLE SOURCE OF TRUTH: main.cpp deliberately does NOT keep its own copy of
// this list any more. It used to (a lowercase `ble_flock_names[]`), which meant
// adding a name to one list silently left the other behind — exactly the drift
// this project's rules warn about. main.cpp now calls fyCheckFlockBleName().
//
// "DfuTarg" is a Nordic legacy-DFU target name. Penguin battery packs advertise
// it while receiving a firmware update (the dump bundles
// no.nordicsemi.android.dfu + heated_battery_fw.bin), so seeing it means a
// Flock battery pack is on the air and mid-update. Added from the firmware-
// derived set (upstream colonelpanichacks/flock-you, 2026-09-16).

static const char* fy_ble_names[] = {
  "FS Ext Battery",
  "Penguin",
  "Flock",
  "Pigvision",
  "Raven",
  "DfuTarg",     // Nordic legacy-DFU target (Penguin pack mid-update)
  nullptr
};

// Pattern (not substring) forms from the same firmware dump. These cannot be
// expressed as substrings: a *bare* 10-digit serial has no distinguishing text
// at all, so it needs a shape check rather than a keyword search.
//   "Penguin-NNNNNNNNNN" — exactly 10 digits after the dash (also caught by the
//                          "Penguin" substring above, but kept explicit so the
//                          serial-bearing form stays recognizable)
//   "NNNNNNNNNN"         — a bare 10-digit serial, nothing else
//   "FS Ext Battery"     — exact
//   "DfuTarg"            — exact (case-insensitive)
// Returns true when the name matches one of those shapes.
static inline bool fyCheckBleNamePattern(const char* name) {
  if (!name || !name[0]) return false;

  // Case-insensitive exact match for the fixed strings.
  if (strcasecmp(name, "fs ext battery") == 0) return true;
  if (strcasecmp(name, "dfutarg") == 0)       return true;

  // "Penguin-" + exactly 10 digits, then end-of-string (case-insensitive).
  if (strncasecmp(name, "penguin-", 8) == 0) {
    const char* p = name + 8;
    int d = 0;
    while (p[d] >= '0' && p[d] <= '9') d++;
    if (d == 10 && p[10] == '\0') return true;
  }

  // Bare 10-digit serial with nothing else.
  {
    int d = 0;
    while (name[d] >= '0' && name[d] <= '9') d++;
    if (d == 10 && name[10] == '\0') return true;
  }
  return false;
}

// ============================================================================
// BLE MANUFACTURER COMPANY ID
// ============================================================================
// Source: wgreenberg/flock-you — 0x09C8 is the XUNTONG BT company ID observed
// in Flock Safety BLE advertisement packets during field testing.
// NOTE: Earlier firmware used 0x05A7 (incorrect).  0x09C8 is the confirmed ID.

static const uint16_t fy_ble_mfr_ids[] = {
  0x09C8   // XUNTONG Technology Co., Ltd  (confirmed Flock Safety BLE)
};
#define FY_BLE_MFR_COUNT (sizeof(fy_ble_mfr_ids)/sizeof(fy_ble_mfr_ids[0]))

// ============================================================================
// RAVEN SURVEILLANCE DEVICE SERVICE UUIDs  (full 128-bit, GainSec research)
// ============================================================================
// Raven is a combined ALPR + gunshot-detection platform sometimes co-deployed
// with Flock cameras.  These GATT service UUIDs were identified by GainSec.

// ── Standard Bluetooth SIG services: NEVER standalone alert evidence ─────────
// 0x180A (Device Information), 0x1809 (Health Thermometer) and 0x1819 (Location
// and Navigation) are *adopted Bluetooth SIG services*, advertised by an
// enormous share of ordinary BLE hardware — phones, watches, earbuds, fitness
// bands, laptops, headsets. 0x180A in particular is present on essentially
// every BLE peripheral ever built.
//
// They appear in GainSec's Raven write-up because Raven firmware 1.1.x
// advertises 0x1809/0x1819 as stand-ins for its own health/location services
// and lists 0x180A alongside the vendor services. They were originally copied
// straight into fy_raven_uuids[] — which made a passing fitness tracker chirp
// and flash as a "Raven camera", because ALERT_BLE_RAVEN_UUID scores
// CS_BLE_UUID_STANDALONE (45), far above CHIRP_MIN_CONFIDENCE (30).
//
// They are therefore kept ONLY as firmware-version evidence (see
// fyEstimateRavenFW() at the bottom of this header) and are never matched on
// their own. fyCheckRavenUUIDFromStrings() additionally consults
// fyService16IsStandardSvc() so a standard service cannot alert even if one is
// re-added to the table by a future edit.
static const uint16_t fy_ble_standard_svcs[] = {
  0x1800, 0x1801, 0x1804, 0x1805, 0x1808, 0x1809, 0x180A, 0x180D, 0x180F,
  0x1810, 0x1811, 0x1812, 0x1813, 0x1814, 0x1815, 0x1816, 0x1818, 0x1819,
  0x181A, 0x181C, 0x181D, 0x181E, 0x181F, 0x1820, 0x1821, 0x1822, 0x1823,
  0x1826, 0x183A
};
#define FY_BLE_STANDARD_SVC_COUNT \
  (sizeof(fy_ble_standard_svcs)/sizeof(fy_ble_standard_svcs[0]))

// True when a 16-bit UUID is an adopted Bluetooth SIG service (i.e. evidence of
// "a BLE device", not of any particular vendor). None of the Raven 0x3100-0x3500
// vendor range is a SIG assignment today, so this is a guard rather than a
// filter for the range path — its real job is to stop a standard service from
// ever alerting via the named table.
static inline bool fyService16IsStandardSvc(uint16_t v) {
  for (size_t i = 0; i < FY_BLE_STANDARD_SVC_COUNT; i++) {
    if (fy_ble_standard_svcs[i] == v) return true;
  }
  return false;
}

// Legacy 16-bit assignments retained for Raven firmware-version estimation only.
#define FY_RAVEN_LEGACY_DEVINFO  "0000180a-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_LEGACY_HEALTH   "00001809-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_LEGACY_LOCATION "00001819-0000-1000-8000-00805f9b34fb"

// Raven VENDOR-SPECIFIC services. Only these may raise a standalone alert:
// 0x3100-0x3500 are not SIG assignments, so a match genuinely indicates this
// product family rather than "some BLE device".
#define FY_RAVEN_GPS          "00003100-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_POWER        "00003200-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_NETWORK      "00003300-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_UPLOAD       "00003400-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_ERROR        "00003500-0000-1000-8000-00805f9b34fb"

static const char* fy_raven_uuids[] = {
  FY_RAVEN_GPS,
  FY_RAVEN_POWER,
  FY_RAVEN_NETWORK,
  FY_RAVEN_UPLOAD,
  FY_RAVEN_ERROR
};
#define FY_RAVEN_UUID_COUNT (sizeof(fy_raven_uuids)/sizeof(fy_raven_uuids[0]))

// Legacy set — deliberately NOT part of fy_raven_uuids[]. Consulted only by
// firmware-version estimation, never by the alerting path.
static const char* fy_raven_legacy_uuids[] = {
  FY_RAVEN_LEGACY_DEVINFO,
  FY_RAVEN_LEGACY_HEALTH,
  FY_RAVEN_LEGACY_LOCATION
};
#define FY_RAVEN_LEGACY_UUID_COUNT \
  (sizeof(fy_raven_legacy_uuids)/sizeof(fy_raven_legacy_uuids[0]))

// ----------------------------------------------------------------------------
// RAVEN SERVICE *RANGE*  (0x3100-0x3500)
// ----------------------------------------------------------------------------
// The table above only names the round hundred values. The camera actually
// advertises services across the whole 0x3100-0x3500 range — and the ones that
// matter most are the ones NOT in that table: 0x3101 / 0x3102 leak GPS
// latitude/longitude unauthenticated (firmware dump, 2026-09-16; upstream
// colonelpanichacks/flock-you flags the same). Pure string comparison against
// the table therefore missed precisely the highest-value services, which is
// the gap fyCheckRavenServiceRange() closes.
#define FY_RAVEN_SVC_MIN 0x3100
#define FY_RAVEN_SVC_MAX 0x3500

// ============================================================================
// FLOCK ACCESSORY / NORDIC DFU SERVICE UUIDs  (firmware dump, 2026-09-16)
// ============================================================================
// Flock's own accessory GATT service — exposed by the Penguin battery packs and
// defined in the camera firmware's GATT definitions — plus the Nordic legacy DFU
// service the pack advertises while receiving a firmware update. Both are full
// 128-bit UUIDs, so they are string-compared like the Raven set above.
//
// NOTE: these live in their own table (and get their own alert type/method)
// rather than being folded into fy_raven_uuids[] because they are *not* Raven
// services — a "ble_raven_uuid" method string for the Flock accessory service
// would mislabel the detection on the dashboard and in CSV export.
#define FY_FLOCK_ACCESSORY_UUID "e8ccbb38-9532-46a8-9fe5-1814df172e6f"
#define FY_NORDIC_DFU_UUID      "00001530-1212-efde-1523-785feabcd123"

static const char* fy_ble_gatt_uuids[] = {
  FY_FLOCK_ACCESSORY_UUID,
  FY_NORDIC_DFU_UUID
};
#define FY_BLE_GATT_UUID_COUNT (sizeof(fy_ble_gatt_uuids)/sizeof(fy_ble_gatt_uuids[0]))

// ============================================================================
// PURE MATCHING FUNCTIONS  (no Arduino / ESP-IDF deps — testable on host)
// ============================================================================

// Returns true if mac_str (e.g. "70:c9:4e:xx:xx:xx") starts with a known
// high-confidence Flock Safety OUI.  First 8 bytes (xx:xx:xx) are compared
// case-insensitively.
static inline bool fyCheckFlockHighMAC(const char* mac_str) {
  if (!mac_str) return false;
  for (size_t i = 0; i < FY_OUI_HIGH_COUNT; i++) {
    if (strncasecmp(mac_str, fy_oui_high[i], 8) == 0) return true;
  }
  return false;
}

// Returns true if mac_str starts with a contract-manufacturer OUI.
static inline bool fyCheckFlockMfrMAC(const char* mac_str) {
  if (!mac_str) return false;
  for (size_t i = 0; i < FY_OUI_MFR_COUNT; i++) {
    if (strncasecmp(mac_str, fy_oui_mfr[i], 8) == 0) return true;
  }
  return false;
}

// Returns true if mac_str exactly equals one of the firmware-default radio
// MACs baked into the Flock camera image (see FY_EXACT_MAC_* above). These are
// high-confidence: the full address is specific even though its OUI is not.
static inline bool fyCheckFlockExactMAC(const char* mac_str) {
  if (!mac_str) return false;
  for (size_t i = 0; i < FY_EXACT_MAC_COUNT; i++) {
    if (strncasecmp(mac_str, fy_exact_macs[i], 17) == 0) return true;
  }
  return false;
}

// Returns true if mac_str starts with a SoundThinking/ShotSpotter OUI.
static inline bool fyCheckSoundThinkingMAC(const char* mac_str) {
  if (!mac_str) return false;
  for (size_t i = 0; i < FY_OUI_ST_COUNT; i++) {
    if (strncasecmp(mac_str, fy_oui_soundthinking[i], 8) == 0) return true;
  }
  return false;
}

// Returns true if 'name' (case-insensitive substring search) matches any
// known Flock/Raven BLE device name pattern.
static inline bool fyCheckBLEName(const char* name) {
  if (!name || !name[0]) return false;
  // Build a lower-case copy of 'name' (max 64 chars)
  char low[64]; size_t i = 0;
  for (; i < 63 && name[i]; i++) {
    char c = name[i];
    low[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
  low[i] = '\0';
  for (const char** kw = fy_ble_names; *kw; kw++) {
    // Walk the keyword in lower-case and use strstr on the lowered name
    char kwl[64]; size_t j = 0;
    for (; j < 63 && (*kw)[j]; j++) {
      char c = (*kw)[j];
      kwl[j] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    kwl[j] = '\0';
    if (strstr(low, kwl)) return true;
  }
  return false;
}

// Returns true if 'id' matches a known Flock Safety BLE manufacturer company ID.
static inline bool fyCheckBLEMfrID(uint16_t id) {
  for (size_t i = 0; i < FY_BLE_MFR_COUNT; i++) {
    if (fy_ble_mfr_ids[i] == id) return true;
  }
  return false;
}

// Single entry point for "is this advertised BLE name a Flock/Raven name?".
// Combines the substring keyword list (fy_ble_names) with the exact/pattern
// shapes (fyCheckBleNamePattern — bare 10-digit serial, Penguin-NNNNNNNNNN,
// FS Ext Battery, DfuTarg). main.cpp calls ONLY this, so the two forms can
// never drift apart.
static inline bool fyCheckFlockBleName(const char* name) {
  return fyCheckBLEName(name) || fyCheckBleNamePattern(name);
}

// ============================================================================
// RAVEN UUID MATCHING  (hardware-independent, string-based)
// ============================================================================

// Local hex helpers — deliberately hand-rolled rather than pulling in
// <ctype.h>/<stdlib.h>, so this header keeps compiling identically on the host
// test build and the ESP32 without extra includes (see the file header).
static inline int fyHexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Extracts the 16-bit service value from either string shape NimBLE / the
// dashboard API hand us for a 16-bit UUID:
//   "00003101-0000-1000-8000-00805f9b34fb"  (canonical 128-bit expansion —
//                                            what NimBLEUUID::toString() emits)
//   "0x3101" or "3101"                       (short form)
// Returns -1 when the string is neither (e.g. a genuine 128-bit vendor UUID).
static inline int fyService16FromUuidString(const char* uuid) {
  if (!uuid) return -1;
  while (*uuid == ' ') uuid++;
  if (uuid[0] == '0' && (uuid[1] == 'x' || uuid[1] == 'X')) uuid += 2;

  static const char* kBase = "-0000-1000-8000-00805f9b34fb";
  size_t n = strlen(uuid);

  // Short form: exactly 4 hex digits.
  if (n == 4) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
      int h = fyHexVal(uuid[i]);
      if (h < 0) return -1;
      v = (v << 4) | h;
    }
    return v & 0xFFFF;
  }

  // Canonical form: 8 hex digits then the Bluetooth base suffix. The 16-bit
  // value sits in the low half of those 8 digits ("00003101" → 0x3101).
  if (n == 8 + strlen(kBase) && strcasecmp(uuid + 8, kBase) == 0) {
    int v = 0;
    for (int i = 0; i < 8; i++) {
      int h = fyHexVal(uuid[i]);
      if (h < 0) return -1;
      v = (v << 4) | h;
    }
    return v & 0xFFFF;
  }
  return -1;
}

// True when a 16-bit service UUID falls in the Raven camera range.
static inline bool fyCheckRavenServiceRange(uint16_t svc16) {
  return svc16 >= FY_RAVEN_SVC_MIN && svc16 <= FY_RAVEN_SVC_MAX;
}

// How strong a Raven-service match is. The two are scored very differently by
// the caller, because a *named* Raven service and "some 16-bit service inside a
// 1025-value block" are not remotely the same evidence:
//   FY_RAVEN_MATCH_NAMED — an exact documented Raven service (fy_raven_uuids[])
//   FY_RAVEN_MATCH_RANGE — any other value in 0x3100-0x3500
enum {
  FY_RAVEN_MATCH_NONE  = 0,
  FY_RAVEN_MATCH_NAMED = 1,
  FY_RAVEN_MATCH_RANGE = 2
};

// Classify the advertised UUID list against the named Raven services and the
// 0x3100-0x3500 range. Sets out_uuid (up to 40 chars) on a match.
//
// WHY THE SPLIT EXISTS — a live false positive (2026-09-21): the range match was
// scored as a standalone Raven camera (CS_BLE_UUID_STANDALONE=45, above the
// chirp threshold), so an unnamed device with a *randomised* MAC at -88 dBm —
// i.e. something weak and far away — chirped and held the alert LED red, logged
// as method=ble_raven_uuid. The 0x3100-0x3500 block is not a Bluetooth SIG
// assignment, so any vendor may use a value in it; matching the whole block is a
// *broad* heuristic, and scoring a broad heuristic as a specific one is the same
// mistake as the mfr-tier/IE-bonus one in fy_confidence.h. Only the documented
// services now alert; an unnamed in-range value is still matched, logged and
// recorded (method=ble_raven_range) but stays below CHIRP_MIN_CONFIDENCE, so it
// can no longer turn the LED red on its own.
static inline int fyClassifyRavenUUIDFromStrings(const char** uuids, int count,
                                                 char* out_uuid) {
  if (!uuids || count <= 0) return FY_RAVEN_MATCH_NONE;
  int best = FY_RAVEN_MATCH_NONE;
  for (int i = 0; i < count; i++) {
    if (!uuids[i]) continue;
    // Standard SIG services are not vendor evidence — skip them outright so a
    // re-added 0x180A/0x1809/0x1819 can never alert again (see the block above
    // fy_raven_uuids[]; this was a real false-positive source).
    int svc = fyService16FromUuidString(uuids[i]);
    if (svc >= 0 && fyService16IsStandardSvc((uint16_t)svc)) continue;
    for (size_t j = 0; j < FY_RAVEN_UUID_COUNT; j++) {
      if (strcasecmp(uuids[i], fy_raven_uuids[j]) == 0) {
        if (out_uuid) strncpy(out_uuid, uuids[i], 40);
        return FY_RAVEN_MATCH_NAMED;   // strongest possible answer
      }
    }
    // Not named — in-range values are recorded but classified as weak.
    if (svc >= 0 && fyCheckRavenServiceRange((uint16_t)svc)) {
      if (out_uuid) strncpy(out_uuid, uuids[i], 40);
      best = FY_RAVEN_MATCH_RANGE;
    }
  }
  return best;
}

// Back-compat wrapper: true for either kind of Raven match. Prefer the
// classifier above where the confidence depends on which kind it was.
static inline bool fyCheckRavenUUIDFromStrings(const char** uuids, int count,
                                               char* out_uuid) {
  return fyClassifyRavenUUIDFromStrings(uuids, count, out_uuid)
         != FY_RAVEN_MATCH_NONE;
}

// Check an array of UUID strings against the Flock accessory / Nordic DFU
// service table (firmware dump, 2026-09-16). Separate from the Raven matcher
// because it is a different alert type with a different method string.
static inline bool fyCheckFlockGattUUIDFromStrings(const char** uuids, int count,
                                                   char* out_uuid) {
  if (!uuids || count <= 0) return false;
  for (int i = 0; i < count; i++) {
    if (!uuids[i]) continue;
    for (size_t j = 0; j < FY_BLE_GATT_UUID_COUNT; j++) {
      if (strcasecmp(uuids[i], fy_ble_gatt_uuids[j]) == 0) {
        if (out_uuid) strncpy(out_uuid, uuids[i], 40);
        return true;
      }
    }
  }
  return false;
}

// ============================================================================
// RAVEN FIRMWARE VERSION ESTIMATION
// ============================================================================
// Estimate Raven firmware version from which service UUID categories are present.
// has_new_gps  = FY_RAVEN_GPS (0x3100) was advertised
// has_old_loc  = FY_RAVEN_LEGACY_LOCATION (0x1819) was advertised
// has_power    = FY_RAVEN_POWER (0x3200) was advertised

static inline const char* fyEstimateRavenFW(bool has_new_gps,
                                             bool has_old_loc,
                                             bool has_power) {
  if (has_old_loc && !has_new_gps) return "1.1.x";
  if (has_new_gps && !has_power)   return "1.2.x";
  if (has_new_gps && has_power)    return "1.3.x";
  return "?";
}

// ============================================================================
// PROBE-REQUEST INFORMATION-ELEMENT FINGERPRINT  (upstream IE signature)
// ============================================================================
// An 802.11 Probe Request whose Information Elements match a known LiteOn/USI
// Flock-chipset fingerprint is a much more specific signal than "a device from
// a Flock OUI sent an empty-SSID probe". The fingerprint is built by walking
// the raw IE TLVs in order and encoding them as a signature string:
//   - SSID (tag 0) is skipped entirely — the *zero length* SSID is what got us
//     here, so its contents carry no information
//   - vendor IE (tag 221) becomes "221:" + the first 8 payload bytes as hex
//   - every other IE becomes its decimal tag number
// For a field-captured camera probe that yields the comma-separated signature
// the upstream project (colonelpanichacks/flock-you) drive-tested:
//   "2,12,127,221:506f9a16030103,45,191,221:0050f208000000"
//
// IMPORTANT — this is a BONUS, not a gate. Upstream REPLACED its plain
// wildcard-probe check with this signature and justified disabling its
// addr1/addr3 tiers on the back of it. This build deliberately does not: a
// camera running firmware we have not fingerprinted still has to be
// detectable, so the IE signature adds confidence on top of
// ALERT_WILDCARD_PROBE (CS_IE_SIG_BONUS in fy_confidence.h) and never removes
// recall. DETECTION_IMPROVEMENTS.md section 7 reaches the same conclusion.
//
// Robustness: ESP32 promiscuous captures of probe requests are frequently
// truncated or skewed (driver length/FCS artifacts), which is why the walk
// below — like upstream's — tolerates a phantom tag-64/len-128 overflow,
// resyncs forward to the next plausible TLV header, and tries three candidate
// spans (full body, body+2 skipping a leading empty-SSID IE, and body with the
// trailing 4-byte FCS removed).
#define FY_IE_TAG_SSID          0
#define FY_IE_TAG_VENDOR        221
#define FY_IE_SIG_MAX           128   // the allowlist entry is ~50 chars
#define FY_IE_VENDOR_HEX_BYTES  8     // payload bytes encoded per vendor IE
#define FY_IE_PHANTOM_SKIP_CAP  16
#define FY_IE_RESYNC_MAX        64
#define FY_IE_PHANTOM_SCAN      32

// Canonical form of the drive-tested signature, as a one-element allowlist so a
// second (firmware-derived) fingerprint can be ADDED later rather than
// replacing this one — see the note above.
static const char* fy_ie_sig_allowlist[] = {
  "2,12,127,221:506f9a16030103,45,191,221:0050f208000000",
  nullptr
};

// Leading tags of the canonical signature and its first vendor-IE token. Used
// to repair a signature whose leading TLVs were lost to parse skew: if the
// vendor anchor is present then everything ahead of it was noise, so the
// canonical prefix can be restored. This is what makes the check work on
// captures that begin mid-IE rather than at the true first IE.
#define FY_IE_CANON_PREFIX  "2,12,127,"
#define FY_IE_CANON_ANCHOR  "221:506f9a16030103"

// Encode n raw bytes as lowercase hex pairs (no separator).
static inline void fyIeHexNibbles(char* dst, const uint8_t* b, int n) {
  static const char hd[] = "0123456789abcdef";
  for (int i = 0; i < n; i++) {
    dst[i * 2]     = hd[b[i] >> 4];
    dst[i * 2 + 1] = hd[b[i] & 0x0f];
  }
}

// Append one comma-separated fragment. False when the size cap is exceeded.
static inline bool fyIeAppend(char* out, size_t cap, size_t* pos,
                              const char* part) {
  size_t plen = strlen(part);
  if (*pos != 0) {
    if (*pos + 1 >= cap) return false;
    out[(*pos)++] = ',';
  }
  if (*pos + plen >= cap) return false;
  memcpy(out + *pos, part, plen);
  *pos += plen;
  out[*pos] = '\0';
  return true;
}

// Append a non-vendor IE as its decimal tag id (e.g. "2", "12", "127").
static inline bool fyIeAppendTag(char* out, size_t cap, size_t* pos,
                                 uint8_t id) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", (unsigned)id);
  return fyIeAppend(out, cap, pos, buf);
}

// Append a vendor IE as "221:" + up to FY_IE_VENDOR_HEX_BYTES payload bytes.
static inline bool fyIeAppendVendor(char* out, size_t cap, size_t* pos,
                                    const uint8_t* payload, int elen) {
  char buf[4 + FY_IE_VENDOR_HEX_BYTES * 2 + 1];
  int take = elen < FY_IE_VENDOR_HEX_BYTES ? elen : FY_IE_VENDOR_HEX_BYTES;
  buf[0] = '2'; buf[1] = '2'; buf[2] = '1'; buf[3] = ':';
  fyIeHexNibbles(buf + 4, payload, take);
  buf[4 + take * 2] = '\0';
  return fyIeAppend(out, cap, pos, buf);
}

// True when ies[pos] begins vendor IE 221 with OUI 50:6f:9a — the LiteOn/Flock
// stack's vendor OUI. Used to recognise a genuine IE boundary inside a run of
// bytes that failed TLV validation.
static inline bool fyIeVendorAt(const uint8_t* ies, int len, int pos) {
  return pos + 9 <= len && ies[pos] == FY_IE_TAG_VENDOR && ies[pos + 1] == 7
      && ies[pos + 2] == 0x50 && ies[pos + 3] == 0x6f && ies[pos + 4] == 0x9a;
}

static inline bool fyIeVendorAhead(const uint8_t* ies, int len, int pos) {
  int end = pos + 2 + FY_IE_PHANTOM_SCAN;
  if (end > len - 1) end = len - 1;
  for (int j = pos + 2; j < end; j++) {
    if (fyIeVendorAt(ies, len, j)) return true;
  }
  return false;
}

// A declared IE length running past the buffer normally means a broken parse —
// but a tag-64/len-128 header with real LiteON payload immediately ahead is a
// known ESP32 capture artifact rather than end-of-frame. Skipping the 2 bogus
// bytes recovers the remainder of the frame.
static inline bool fyIeIsPhantomOverflow(const uint8_t* ies, int len,
                                         uint8_t id, int elen, int i) {
  if (i + 2 + elen <= len) return false;
  if (elen > 200) return true;
  return id == 64 && elen == 128 && fyIeVendorAhead(ies, len, i);
}

// Slide forward up to FY_IE_RESYNC_MAX bytes looking for the next plausible TLV
// header (an id + length that fits inside the buffer).
static inline int fyIeResync(const uint8_t* ies, int len, int start) {
  int end = start + FY_IE_RESYNC_MAX;
  if (end > len - 1) end = len - 1;
  for (int j = start; j < end; j++) {
    int elen = (int)ies[j + 1];
    if (elen <= 200 && j + 2 + elen <= len) return j;
  }
  return -1;
}

// Walk IE TLVs into a signature string. Sets *complete when every byte was
// consumed (a clean parse), which the caller uses to prefer the better of two
// candidate parses.
static inline bool fyIeSigFromIes(const uint8_t* ies, int len, char* out,
                                  size_t cap, bool* complete) {
  if (!ies || len < 2 || !out || cap < 4) return false;
  size_t pos = 0;
  out[0] = '\0';
  int i = 0;
  uint8_t phantomSkips = 0;
  while (i + 2 <= len) {
    uint8_t id   = ies[i];
    int     elen = (int)ies[i + 1];
    if (i + 2 + elen > len) {
      if (phantomSkips < FY_IE_PHANTOM_SKIP_CAP
          && fyIeIsPhantomOverflow(ies, len, id, elen, i)) {
        phantomSkips++;
        i += 2;
        continue;
      }
      int j = fyIeResync(ies, len, i);
      if (j > i) { i = j; continue; }
      return false;
    }
    i += 2;
    if (id == FY_IE_TAG_SSID) {
      // A zero-length SSID IE is followed by other IEs, not more SSID data, so
      // absorb any immediately-repeated empty SSID pairs before moving on.
      if (elen == 0) {
        while (i + 2 <= len && ies[i] == 0 && ies[i + 1] == 0) i += 2;
      } else {
        i += elen;
      }
      continue;
    }
    if (id == FY_IE_TAG_VENDOR && elen >= 4) {
      if (!fyIeAppendVendor(out, cap, &pos, ies + i, elen)) return false;
    } else {
      if (!fyIeAppendTag(out, cap, &pos, id)) return false;
    }
    i += elen;
  }
  if (complete) *complete = (i == len);
  return pos > 0;
}

// Prefer the cleaner of two candidate parses: a complete parse beats an
// incomplete one, then the longer signature wins (more IEs recovered).
static inline bool fyIePickSig(const char* a, bool aComplete,
                               const char* b, bool bComplete,
                               char* out, size_t cap) {
  if (!a[0] && !b[0]) return false;
  const char* pick = a;
  if (a[0] && !b[0])              pick = a;
  else if (!a[0] && b[0])         pick = b;
  else if (aComplete != bComplete) pick = aComplete ? a : b;
  else if (strlen(b) > strlen(a))  pick = b;
  strncpy(out, pick, cap - 1);
  out[cap - 1] = '\0';
  return true;
}

// Restore the canonical leading tags when the vendor anchor survived but the
// leading TLVs did not (see FY_IE_CANON_ANCHOR).
static inline void fyIeCanonicalize(char* sig, size_t cap) {
  if (!sig || cap < 16) return;
  const size_t prefixLen = strlen(FY_IE_CANON_PREFIX);
  if (strncmp(sig, FY_IE_CANON_PREFIX, prefixLen) == 0
      && strstr(sig, FY_IE_CANON_ANCHOR) != nullptr) {
    return;   // already canonical
  }
  const char* anchor = strstr(sig, FY_IE_CANON_ANCHOR);
  if (!anchor) return;
  char tmp[FY_IE_SIG_MAX];
  int n = snprintf(tmp, sizeof(tmp), "%s%s", FY_IE_CANON_PREFIX, anchor);
  if (n > 0 && (size_t)n < cap) memcpy(sig, tmp, (size_t)n + 1);
}

// Build the signature for a Probe Request body (its IEs start at body[0]).
static inline bool fyIeSigFromProbeBody(const uint8_t* body, int bodyLen,
                                        char* out, size_t cap) {
  if (!body || bodyLen < 2 || !out || cap < 16) return false;
  char sigA[FY_IE_SIG_MAX] = {0};
  char sigB[FY_IE_SIG_MAX] = {0};
  bool completeA = false, completeB = false;
  bool okA = fyIeSigFromIes(body, bodyLen, sigA, sizeof(sigA), &completeA);
  bool okB = false;
  // Some captures include the leading empty-SSID IE, some begin after it.
  if (bodyLen >= 2 && body[0] == 0 && body[1] == 0) {
    okB = fyIeSigFromIes(body + 2, bodyLen - 2, sigB, sizeof(sigB), &completeB);
  }
  char merged[FY_IE_SIG_MAX] = {0};
  if (!fyIePickSig(okA ? sigA : "", completeA, okB ? sigB : "", completeB,
                   merged, sizeof(merged))) {
    return false;
  }
  fyIeCanonicalize(merged, sizeof(merged));
  strncpy(out, merged, cap - 1);
  out[cap - 1] = '\0';
  return out[0] != '\0';
}

// True when a signature equals any allowlist entry.
static inline bool fyIeSigInAllowlist(const char* sig) {
  if (!sig || !sig[0]) return false;
  for (size_t i = 0; fy_ie_sig_allowlist[i]; i++) {
    if (strcmp(sig, fy_ie_sig_allowlist[i]) == 0) return true;
  }
  return false;
}

// Entry point for the sniffer: does this Probe Request body carry the
// drive-tested Flock LiteOn/USI IE fingerprint? Only called after the frame has
// already matched a high/mfr-tier OUI *and* a zero-length SSID, so this is a
// pure confidence refinement — a false negative costs only the bonus.
static inline bool fyCheckFlockIeSignature(const uint8_t* body, int bodyLen) {
  if (!body || bodyLen < 2) return false;
  char sig[FY_IE_SIG_MAX];
  if (fyIeSigFromProbeBody(body, bodyLen, sig, sizeof(sig))
      && fyIeSigInAllowlist(sig)) {
    return true;
  }
  // The final 4 bytes of a promiscuous capture are frequently the FCS, which is
  // not an IE; retry without them (upstream does the same).
  if (bodyLen > 4
      && fyIeSigFromProbeBody(body, bodyLen - 4, sig, sizeof(sig))
      && fyIeSigInAllowlist(sig)) {
    return true;
  }
  return false;
}

#endif // FY_DETECT_H
