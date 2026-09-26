# Changelog

All notable user-visible changes to flock-you-esp32 are recorded here. The
format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
this project uses [semantic versioning](https://semver.org/) on the firmware's
`vN` banner (see `[flockyou] vN WiFi detector started` at boot).

Rationale for a root-cause note on most entries: this project has repeatedly
fixed the same class of bug more than once (silently-swallowed return values,
scoring changes that lift a deliberately-quiet tier over the chirp threshold,
blocking calls disguised as async). A future reader should be able to tell
*why* an entry exists without re-deriving the investigation — see
`.clinerules/` for the durable version of those lessons.

## [Unreleased]

### Added

- **Web-flasher configurator** (ADR-0001): before flashing, choose which
  detections this device watches for, which outputs it uses (light, sound,
  vibration — only the ones the selected board actually has), and two bounded
  sensitivity settings (alert threshold, Bluetooth proximity floor). Everything
  defaults to the current behaviour, so leaving it alone changes nothing.
  - Stored in a new `fycfg` partition written by the flasher, in a
    magic/version/CRC32-wrapped struct (`fy_config.h`). Erased or corrupt config
    means "no user configuration" and falls back to the compile-time defaults.
  - Detections are gated at `enqueueAlert()` — the single funnel — so a disabled
    engine is off for logging, dashboard, CSV and alerts alike, and a future
    engine cannot forget to honour the setting. Suppressions are counted as
    `cfgskip=` in the stats line, so "you turned this off" is distinguishable
    from "the firmware never saw it".
  - Sensitivity can only be made *quieter*, never louder: the configured
    threshold is applied on top of the compile-time floor, because that floor is
    what keeps shared-manufacturer hardware from alerting (see the three
    stuck-red fixes below).
  - The config loads before display init, so even the Core2 startup vibration
    honours a "no vibration" choice.
- **Firmware-derived signature set** (Flock Safety ALPR camera dump: Qualcomm
  MSM8953 + QCA9377, Android 8.1 "hpnotiq", 2026-09-16), kept as a **union**
  with the community OUI list rather than a replacement — the two sets target
  different hardware generations and barely overlap:
  - `ALERT_FW_DEFAULT_MAC` — exact match on the factory-default radio MACs
    (`00:03:7f:50:00:01`, `00:03:7f:4f:00:16`). Scored 55, and only ever fires
    on a never-provisioned unit.
  - `ALERT_BLE_FLOCK_GATT` — Flock accessory service
    `e8ccbb38-…` and Nordic legacy DFU `00001530-…`.
  - Raven service **range** `0x3100`–`0x3500` swept, catching `0x3101`/`0x3102`
    (the unauthenticated GPS-leaking services the named list missed).
  - BLE name **shapes** — a bare 10-digit serial, `Penguin-`+10 digits,
    `FS Ext Battery`, `DfuTarg`.
  - SSID keyword `fs ext battery`.
  - `device_name` is now populated in BLE JSON events (it was hardcoded empty).
- **Arrival-vs-match diagnostics** (`FY_SNIFF_STATS`, on by default, compiled
  out with `0`). Cheap counters reported in the 30 s heartbeat that separate
  "the frame never physically arrived" from "it arrived and failed to match" —
  the question static review could not answer for the alert types that were
  never caught in cross-device testing.
- **Beacon-tester TX-error reporting** — `esp_wifi_80211_tx()` and
  `esp_wifi_set_channel()` return values are now checked and reported
  (rate-limited), so a tester-side TX refusal is no longer indistinguishable
  from a detector-side miss.
- **Printable quick-start guides** — 4×6 pocket card and 8.5×11 sheet
  (`docs/print/`), generated from one source by a checked-in script.
- **Fourth OUI tier** and an expanded SSID keyword list.

### Changed

- **The Raven `0x3100`–`0x3500` range is now scored as a weak tier, not a
  standalone camera.** The named list holds only the round hundred values, so
  the range must still be matched to catch `0x3101`/`0x3102` — but scoring an
  in-range match at 45 produced a live false positive: an unnamed device with a
  randomised MAC at −94 dBm chirped and held the alert LED red. Range-only
  matches are now recorded, logged and exported at 20 (below the chirp
  threshold) so they stay visible without being able to alert.
- **The IE-fingerprint bonus is gated to high-tier OUIs.** Applied to the
  contract-manufacturer tier it lifted 20 → 38, past the chirp threshold —
  converting a tier that exists specifically to stay quiet into one that chirps
  and flashes. This is the third scoring change to trip that class of bug.
- BLE manufacturer company IDs and SSID keywords moved into `fy_detect.h` as
  the single source of truth; `main.cpp` had private copies of both, which had
  already drifted (`flck` and the bare-serial name form were invisible to the
  duplicate list).
- Community OUI list sync: added `14:b5:cd`, added the CVE-2025-59409 SSID
  spelling `flck` (which does not contain "flock", so it needed its own entry).

### Fixed

- **VoiceS3R flash verification** — `flash.sh` and `flash_voices3r.sh` now read
  for a `[flockyou]` boot line before declaring success. They previously
  declared "device running" from port presence alone, which reported success
  for a board still sitting in the ROM download mode (the ESP32-S3 USB-JTAG
  peripheral advertises the same VID:PID in both states).
- **BLE-only detections never alerted.** A BLE match used to only record a
  timestamp for a later WiFi correlation bonus and never called
  `enqueueAlert()` — so BLE-only Flock signals produced no LED, chirp, log line
  or JSON at all.
- **Display flicker** — the UI task treated the ~250 ms clock tick as a data
  change and cleared+redrew the whole content area ~4×/second, even when idle.
- **Stuck-red LEDs at boot** (M5Atom Lite/Voice).
- **`NimBLEScan::start()` overload resolution** — a blocking overload was being
  selected where an async one was intended, hanging every `BLE_COEX_MODE` build
  at `setup()`. Calls are now explicitly typed.
- **Silent audio on Atom VoiceS3R / Echo S3R** — `M5.Speaker.begin()` is now
  called explicitly and its failure logged, because M5Unified's lazy-init path
  returns success even when the codec/I2S setup fails.
- Dead documentation links removed (several pointed at files that never existed
  in this repository) and the emoji-heading anchor bug fixed.

### Removed

- **The untested 3D-printed case design and its render tooling**
  (`CASE_DESIGN.md`, `hardware/openscad/`, `RENDERING_GUIDE.md`,
  `render_models.sh`, generated renders). It was authored against a breadboard
  build, never printed or fitted, and shipped with print settings and a
  filament cost table — which made it look validated when it was not. No
  validated enclosure is published; the hardware directory now says so.

## [v2] — earlier

- Split the monolith into single-purpose headers (`fy_detect.h`,
  `fy_confidence.h`, `led_gpio.h`, `led_neopixel.h`, `ui_task.h`, board
  `*_display.h`, `ble_selftest.h`) and moved UI/display rendering onto its own
  FreeRTOS task.
- M5Stack Basic Core v2.7, Core2 For AWS and M5StickC Plus SE support, with
  Core2 vibration and StickC LED alert feedback.
- WiFi signal bars, trend arrows and channel lock; faster channel hop.
- Beacon-tester firmware (`m5atom-lite-beacon`) for two-board cross-device
  testing, plus a self-test build (`m5atom-lite-ble-selftest`).
- Web flasher exposing every firmware variant, defaulting to the BLE builds.
- ESP32-C5 (dual-band) build support — experimental.
- Apache 2.0 license with attribution.

