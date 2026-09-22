# Clean Code Rules — flock-you-esp32

These rules apply to all C/C++ firmware code (`*.cpp`, `*.h`) and Python
tooling (`api/*.py`) in this repository.

## General

- **Comment the "why", not just the "what".** This codebase has repeatedly
  been bitten by subtle bugs (wrong overload resolution, blocking calls
  disguised as async ones, HCI restrictions, radio coexistence quirks).
  When you fix a non-obvious bug, leave a comment at the fix site
  explaining the root cause and symptom, not just what changed. Future
  agents (and humans) must be able to understand *why* code looks the way
  it does without re-deriving the investigation from scratch.
- **Never silently swallow an error return value.** If a function returns
  `bool`/an error code and that return is ignored, and the failure mode is
  not obviously harmless, log it. This project has been bitten by exactly
  this (`adv->start()`'s return value being ignored masked a completely
  silent BLE transmission failure).
- **M5Unified's lazy-init APIs can silently swallow failures too.**
  `M5.Speaker.tone()` lazily calls `Speaker_Class::begin()` on first use if
  it hasn't been called yet — but `_play_raw()`'s lazy-init guard
  (`if (!begin() || (_task_handle == nullptr)) { return true; }`) returns
  *success* even when that lazy `begin()` call fails (e.g. codec I2C
  enable-register write or I2S peripheral setup failing). The result is
  total audio silence with zero error trace anywhere — indistinguishable
  from "speaker working but nothing audible" until you go read
  `Speaker_Class.cpp` yourself. Any board wired through `M5.Speaker` must
  call `M5.Speaker.begin()` explicitly in `setup()` and log a failure if it
  returns `false`, rather than relying on the lazy path (see `main.cpp`'s
  `USE_M5ATOM_VOICES3R` setup() block, which also logs `M5.getBoard()` so a
  board-auto-detection failure — a separate silent-failure mode where
  M5Unified's I2C board-ID probe misses and falls back to a board with no
  audio pins configured at all — is distinguishable from an init failure on
  a correctly-detected board).
- **M5Unified board auto-detection can silently mis-ID hardware, with
  cascading effects.** Boards without a strapped ID (e.g. ESP32-S3 LGA56
  parts like Atom VoiceS3R/Echo S3R) are identified in
  `M5Unified.cpp::_check_boardtype()` by probing for known I2C peripherals
  (e.g. an ES8311 codec at address 0x18 on SDA=45/SCL=0 identifies
  `board_M5AtomVoiceS3R`). If that probe fails for any reason (bad solder
  joint, I2C bus contention, etc.), M5Unified falls back to a *different*
  board identity in the same detection cascade (e.g. `board_M5StampS3Mini`)
  that has NO speaker/mic pin configuration at all in `_begin_audio()` —
  producing silent audio with no error anywhere, indistinguishable from a
  `Speaker.begin()` failure on correctly-detected hardware. Always log
  `M5.getBoard()` right after `M5.begin()` on any board relying on
  auto-detection, so this failure mode is distinguishable from others in
  serial output.
- **`board_M5AtomVoiceS3R` has no addressable status LED in any published
  M5Unified version.** Confirmed by diffing M5Unified's RGB-LED pin table
  (`_pin_table_other0[]`) between the project's pinned version (0.2.19) and
  the latest published version (0.2.20 as of this writing) — neither lists
  an LED GPIO for this board, unlike Atom Lite/Matrix/Voice (SK6812 on
  GPIO27). This is a genuine hardware limitation of the Atom VoiceS3R/Echo
  S3R module (audio-only, no discrete RGB LED), not a library version-lag
  bug — do not "fix" it by bumping the M5Unified version pin.

- **A scoring change must be checked against the confidence TIER it applies to,
  not just against the signal it rewards.** A flat bonus/weight can lift a tier
  that is deliberately held *below* `CHIRP_MIN_CONFIDENCE` up over it, silently
  converting a tier designed to log quietly into one that chirps and flashes.
  This project has now been bitten twice: the mfr-tier wildcard-probe case
  (recorded in `computeConfidence()`), and the IE-fingerprint bonus — applied to
  every OUI tier, it took mfr hits from `CS_OUI_MFR` (20, silent) to 38, over the
  chirp threshold, until it was gated on `isHigh`. The symptom in both cases is
  LEDs that appear **permanently stuck red**, because the extra detections
  re-trigger `ledFlash()` faster than it can expire. Before adding a weight,
  state which tiers it can reach and compute the resulting score at *each tier's
  existing floor*.
- **Prefer explicit over implicit in ambiguous API calls.** When a C++ API

  has multiple overloads that could plausibly be selected by argument
  types (e.g. `NimBLEScan::start(uint32_t, bool)` vs.
  `start(uint32_t, callback, bool)`), make the call unambiguous — pass an
  explicit typed value/cast rather than relying on default-argument or
  implicit-conversion resolution. This bit the entire BLE_COEX_MODE code
  path project-wide; see `main.cpp`'s `bleCoexStart()` fix.
- **No magic numbers without a named `#define`/`constexpr`.** Timing
  constants, thresholds, RSSI cutoffs, etc. must be named constants at the
  top of the relevant section, not inline literals.
- **Match existing naming conventions** in the file/module you're editing
  (`fy*` prefix for detection-pipeline functions, `ble*` prefix for BLE
  helpers, `bf*` prefix for `beacon_frames.h`, etc.) rather than
  introducing a new style.
- **Keep ISR/callback-context code minimal.** Code that runs in
  `IRAM_ATTR` functions, WiFi promiscuous callbacks, or BLE host-task
  callbacks must avoid `Serial.print`, dynamic allocation, and anything
  that isn't safe from an interrupt/foreign-task context. Push data through
  the existing lock-free alert queue (`enqueueAlert()`/`drainAlertQueue()`)
  instead.
- **Never `++`/`--` a `volatile` counter — use `+= 1` instead.** This
  project's ESP32 Arduino core compiles as gnu++20, where increment/
  decrement of a volatile-qualified object is deprecated (P1152R4,
  diagnosed as `-Wvolatile`). Adding the `FY_SNIFF_STATS` counters to
  `main.cpp` with `++` produced 23 of these warnings in one commit; the
  pre-existing `ui_task.h`'s `g_uiAlertSeq++` had been emitting one all
  along. Compound assignment is *not* deprecated and is semantically
  identical for a scalar counter — `main.cpp` routes every counter bump
  through its `FY_STAT_BUMP()` macro for exactly this reason. Verify any
  new counter by checking that a build of the env that compiles the file
  reports **zero** `warning:` lines, not just zero errors.
- **One source of truth per detection-pattern table.** All pattern data lives
  in `fy_detect.h` and nowhere else. `main.cpp` previously kept a *second*,
  lowercase copy of the BLE name list (`ble_flock_names[]`, matched by its own
  local `bleNameContains()`), so adding a name to one list silently left the
  other behind — and a naming form only the pattern matcher can express (a bare
  10-digit serial, `Penguin-NNNNNNNNNN`, `DfuTarg`) was invisible to the
  substring copy. Both duplicates are gone; `main.cpp` now calls
  `fyCheckFlockBleName()`. The beacon tester likewise derives its payloads from
  the shared tables (`fy_exact_macs[]`, `FY_FLOCK_ACCESSORY_UUID`,
  `fy_ble_names[]`) instead of re-hardcoding them. When a signature needs a
  *shape* rule rather than a substring, add it to the shared header so the host
  unit tests (`pio test -e native`) can cover it.
  The same consolidation has since been applied to the other two tables that
  had drifted or were about to: the **SSID keyword list** (was a private
  `target_ssid_keywords[]` copy inside `main.cpp`, now `fy_ssid_keywords[]` +
  `fyCheckFlockSsidKeyword()`) and the **BLE manufacturer company ID** (was the
  literal `0x09C8` compared inline in two places, now `fy_ble_mfr_ids[]` +
  `fyCheckBLEMfrID()`). Rule of thumb: if a value decides whether a detection
  fires, it belongs in `fy_detect.h`, and `main.cpp` should only call a matcher.

## Python (`api/`)

- Follow PEP 8. Use type hints on new functions.
- No bare `except:` — catch specific exceptions.

## Reviewing your own changes

Before considering a change complete, re-read the diff and ask:
1. Does every changed line have an obvious reason to exist?
2. Would a future engineer understand *why*, not just *what*, from reading
   the surrounding comments?
3. Did I leave any debug-only logging in that should be removed or gated?
