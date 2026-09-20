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
//   Raven UUIDs — GainSec research (8 full 128-bit service UUIDs)
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

#define FY_RAVEN_DEVICE_INFO  "0000180a-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_GPS          "00003100-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_POWER        "00003200-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_NETWORK      "00003300-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_UPLOAD       "00003400-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_ERROR        "00003500-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_OLD_HEALTH   "00001809-0000-1000-8000-00805f9b34fb"
#define FY_RAVEN_OLD_LOCATION "00001819-0000-1000-8000-00805f9b34fb"

static const char* fy_raven_uuids[] = {
  FY_RAVEN_DEVICE_INFO,
  FY_RAVEN_GPS,
  FY_RAVEN_POWER,
  FY_RAVEN_NETWORK,
  FY_RAVEN_UPLOAD,
  FY_RAVEN_ERROR,
  FY_RAVEN_OLD_HEALTH,
  FY_RAVEN_OLD_LOCATION
};
#define FY_RAVEN_UUID_COUNT (sizeof(fy_raven_uuids)/sizeof(fy_raven_uuids[0]))

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

// Check an array of UUID strings against the known Raven service UUID list,
// and against the 0x3100-0x3500 Raven service *range* (see FY_RAVEN_SVC_MIN).
// Returns true on first match; sets out_uuid (up to 40 chars) if provided.
// uuids[] must be lowercase or the comparison will still work because
// strcasecmp is used.
static inline bool fyCheckRavenUUIDFromStrings(const char** uuids, int count,
                                               char* out_uuid) {
  if (!uuids || count <= 0) return false;
  for (int i = 0; i < count; i++) {
    if (!uuids[i]) continue;
    for (size_t j = 0; j < FY_RAVEN_UUID_COUNT; j++) {
      if (strcasecmp(uuids[i], fy_raven_uuids[j]) == 0) {
        if (out_uuid) strncpy(out_uuid, uuids[i], 40);
        return true;
      }
    }
    // Not one of the named services — but any in-range 16-bit service is a
    // Raven camera advertiser (this is what catches 0x3101/0x3102).
    int svc = fyService16FromUuidString(uuids[i]);
    if (svc >= 0 && fyCheckRavenServiceRange((uint16_t)svc)) {
      if (out_uuid) strncpy(out_uuid, uuids[i], 40);
      return true;
    }
  }
  return false;
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
// has_old_loc  = FY_RAVEN_OLD_LOCATION (0x1819) was advertised
// has_power    = FY_RAVEN_POWER (0x3200) was advertised

static inline const char* fyEstimateRavenFW(bool has_new_gps,
                                             bool has_old_loc,
                                             bool has_power) {
  if (has_old_loc && !has_new_gps) return "1.1.x";
  if (has_new_gps && !has_power)   return "1.2.x";
  if (has_new_gps && has_power)    return "1.3.x";
  return "?";
}

#endif // FY_DETECT_H
