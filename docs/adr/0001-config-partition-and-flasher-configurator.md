# ADR-0001: Runtime config partition and web-flasher configurator

**Status:** Accepted — implemented for the firmware and the web flasher
**Date:** 2026-09-19
**Supersedes:** none

## Context

Users want to choose, before flashing, **which detections are active**, tune
**sensitivity**, and enable or disable each **output** (LED, chirp, vibration,
on-screen hold). Today every one of those knobs is a compile-time constant, so a
change means a maintainer edits `fy_detect.h`/`fy_confidence.h`, rebuilds, and
the user reflashes — the user cannot adjust anything themselves, and we cannot
ship per-user firmware without exploding the build matrix.

The constraints that shape the answer:

- **No combinatorial variants.** There are 20 firmware environments and 13
  detection paths with independent enable/disable, plus several output toggles.
  Pre-building every combination is not viable (2^N), and even a curated set
  would fragment the flasher and CI.
- **The flasher is a static site.** `docs/index.html` is served from GitHub
  Pages and flashes via **ESP Web Tools** (`esp-web-install-button`). There is no
  server to compile or personalise a build, and we want to keep it that way.
- **ESP Web Tools manifests accept arbitrary parts.** A manifest's `parts` array
  is just `{offset, file}` pairs, so a binary can be written to an arbitrary
  flash offset alongside the firmware. The manifest itself does not have to be a
  static file — it can be generated in-page and handed to the button as an
  object URL.
- **The partition tables are full.** flock's `spiffs` ends exactly at 8 MB;
  there is no free space to claim without moving something.

## Decision

**Add a dedicated `fycfg` data partition, hold configuration in a fixed-layout
struct protected by a magic number and a CRC32, and have the flasher generate
that blob from the user's choices and flash it as an extra manifest part.**

1. **Storage: a new `fycfg` partition** (data type, custom subtype, fixed
   offset, 0x1000 = 4 KB). Firmware reads it once at boot with
   `esp_partition_read()` — no filesystem, no NVS page format, nothing to
   corrupt into an unreadable state.
2. **Layout:** `magic | version | length | payload | crc32(payload)`. A blank
   (freshly erased `= 0xFF`) or bad-CRC partition is **not an error**: it means
   "no user config", and the firmware falls back to the compile-time defaults
   that represent today's behaviour. So a plain flash with no config behaves
   exactly as it does now, and a corrupt config degrades to defaults rather than
   to something undefined.
3. **The flasher builds the blob in JS** from checkbox/slider state, then flashes
   it as an extra part. It can also write *only* the config partition to change
   settings later, without reflashing the firmware.
4. **The schema is versioned in the blob.** Firmware that sees a *newer* version
   than it knows ignores the blob and uses defaults — it must never reinterpret
   bytes it does not understand.
5. **Sensitivity is clamped to the ranges the scoring model assumes.** This is
   not cosmetic: the tiers are load-bearing. Letting a user apply a flat bonus to
   a deliberately-quiet tier is exactly what made status LEDs appear permanently
   stuck red, three separate times (contract-manufacturer wildcard probes, the
   IE-fingerprint bonus, the Raven UUID range). The configurator may expose
   "quieter / default / louder" presets and per-engine on/off, but **must not
   expose a free-form score weight**, and the firmware clamps anything out of
   range rather than trusting the blob.

## Consequences

**Gains**

- One firmware per board, and the user's choices are theirs — no maintainer
  rebuild, no variant explosion, no CI matrix growth.
- Settings survive a firmware reflash (different partition), so an update does
  not silently reset a user's preferences.
- The same mechanism later serves "change my settings" without touching the app,
  because writing one 4 KB partition is fast.
- CRC32 bootstraps the session-persistence integrity work: the same envelope
  should protect the SPIFFS detection session, which currently has **no**
  integrity check at all and would be silently misparsed after a power loss
  mid-write.

**Costs / risks**

- **A partition-table change.** `spiffs` must shrink to make room (`0x1F0000` →
  `0x1E0000`). Offsets of `nvs`, `otadata` and `app0` are preserved so existing
  OTA slots and NVS contents stay valid, but anyone updating from the old table
  must full-flash. The web flasher does exactly that, and `flash.sh` already
  erases.
- More code on the boot path, so it needs host-testable codec functions and a
  two-board verification — the config path failing must not stop the detector
  from starting.
- A user can make the device *less* sensitive and then report missed detections.
  Mitigation: expose presets rather than raw numbers, label the default clearly,
  and have the boot banner print whether a user config was loaded (so support
  can distinguish "user dialled it down" from "broken").
- Two sources of truth for behaviour (compile-time defaults and runtime config).
  Mitigation: defaults live in exactly one place and the config layer may only
  *override* them, never redefine them.

## Alternatives considered

- **Pre-built firmware variants per combination** — rejected: combinatorial, and
  it puts the choice in our build system instead of the user's hands.
- **Write an NVS image from the flasher** — rejected: NVS requires generating
  pages with its own entry format and CRC semantics client-side; far more
  fragile than a flat struct, for no benefit.
- **A LittleFS/SPIFFS config file** — rejected: needs a whole filesystem image
  at build time and a filesystem mount on the boot path, to store 20 bytes.
- **Runtime settings over serial (as upstream's dashboard does)** — rejected as
  the *primary* mechanism: it requires a host running the Flask dashboard, so a
  user who just flashed from the web gets nothing. Worth adding later *on top
  of* this, not instead of it.
- **BLE configuration from a phone app** — rejected: no app exists for this
  project, and it would make configuration unavailable until one does.

## Verification plan

1. Host unit tests for the codec: round-trip, bad magic, bad CRC, truncated
   payload, unknown-newer version, and bounds clamping of every field.
2. Boot with the partition erased (defaults used, behaviour unchanged) and with
   a valid config (values applied), confirmed via the boot banner.
3. Two-board test: a config with one detection disabled must stop that method
   firing while others still do, measured with the existing `FY_SNIFF_STATS`
   arrival/gate counters.
4. Confirm a firmware reflash over an existing `fycfg` preserves settings.

## Implementation notes (as built)

- Written into the 64 KB that was already unallocated at the end of the 4 MB
  layout, so **no existing partition offset moved** and `spiffs` kept its full
  size — the "cost" listed above turned out not to be necessary.
- The engine gate lives in `enqueueAlert()`, the single funnel every detection
  passes through, rather than at the 16 call sites. One check covers every
  current path and any future one, so a new engine cannot forget to honour a
  user's "disable this" choice.
- The configured threshold is applied as an **additional** gate on top of the
  compile-time one, so it can only make a device quieter. It can never lower the
  effective floor below `CHIRP_MIN_CONFIDENCE`, which is what keeps the
  contract-manufacturer tier silent (see the stuck-red note above).
- Suppressed detections are counted (`cfgskip=` in the stats line) so support can
  distinguish "your config turned this off" from "the firmware never saw it".
- `m5basicVibrationStop()` exists because simply skipping
  `m5basicVibrationTick()` would leave the motor energised if a pulse was in
  flight — a stuck vibrator is the same defect class as the stuck-red LEDs.
- The config is loaded at the very top of `setup()`, before display init, so the
  Core2 *startup* vibration pulses also honour a "no vibration" choice.

**Cross-language verification of the blob layout** (the one thing a single-language
test cannot prove): the real JS was extracted from `docs/index.html` and run under
Node to emit a blob, which was then decoded by the real C codec on the host. Both
sides produce the standard CRC-32 check value `0xCBF43926`, and the decoded
fields (flags, engine mask, threshold, signed RSSI floor) matched exactly.

