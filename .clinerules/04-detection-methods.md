# Detection Methods Reference — flock-you-esp32

This is a living reference of every detection path the firmware
implements, so agents don't have to re-derive the full picture from
scattered code each session. **Keep this file in sync whenever a
detection path is added, removed, or its scoring changes** (see the
self-updating meta-rule in `05-keep-rules-current.md`).

All detections funnel through `enqueueAlert()` → `drainAlertQueue()` →
LED flash / chirp (if `confidence >= CHIRP_MIN_CONFIDENCE`, currently 30)
/ JSON emission (`emitDetectionJSON()`) / SPIFFS session persistence.

## WiFi detections (`wifiSniffer()` in `main.cpp`, patterns in `fy_detect.h`)

| AlertType             | Trigger                                                                 | Method string      | Notes |
|-----------------------|--------------------------------------------------------------------------|--------------------|-------|
| `ALERT_OUI_ADDR2`     | 802.11 `addr2` (transmitter) matches a high-confidence Flock OUI          | `oui_addr2`        | Primary/strongest WiFi signal |
| `ALERT_FW_DEFAULT_MAC`| `addr2` equals, byte-for-byte, one of `fy_exact_macs[]` — the **factory-default** QCA9377 radio MACs from the camera firmware image (`00:03:7f:50:00:01`, `00:03:7f:4f:00:16`) | `fw_default_mac` | Firmware-derived (2026-09-16). `CS_FW_DEFAULT_MAC=55`. Checked *before* the OUI tiers because those MACs live inside the ubiquitous `00:03:7f` Qualcomm Atheros prefix (mfr-tier, 20, silent) — the full address is the specific part, so the mfr-tier emission is suppressed for the same frame. Only ever fires on an **unprovisioned** unit (provisioning rewrites the MAC). |
| `ALERT_OUI_ADDR1`     | `addr1` (receiver/dest) matches a high-confidence OUI, not multicast      | `oui_addr1`        | Catches cameras appearing as probe-response destinations |
| `ALERT_OUI_ADDR3`     | `addr3` (BSSID) matches, mgmt frames only, not multicast                 | `oui_addr3`        | Fallback for randomized `addr2` |
| `ALERT_WILDCARD_PROBE`| Probe Request with high/mfr-tier OUI **and** a zero-length (wildcard) SSID IE | `wildcard_probe` | Flock cameras scan with empty-SSID probes. **+18 (`CS_IE_SIG_BONUS`) when the IEs also match the drive-tested LiteOn/USI fingerprint** — see the IE note below. |
| `ALERT_SSID`          | Beacon/Probe-Resp/Probe-Req SSID contains a keyword (`flock`, `flocksafety`, `penguin`, `pigvision`, `fs ext battery`, `flck`), globally-administered MAC | `ssid` | `fs ext battery` added from the firmware-derived set (FS Ext Battery pack SoftAP). `flck` exists only for the truncated **CVE-2025-59409** spelling `test_flck`, which does *not* contain `flock` (f-l-c-k vs f-l-o-c-k), so without its own entry such a camera is invisible to the SSID path. |
| `ALERT_LAA_SSID`      | Same SSID match, but transmitter MAC is **locally-administered** (bit 1 of first octet set) | `laa_ssid` | Issue-#43 "Flock Camera net." camera class — LAA MACs never match any OUI table, so SSID is the only handle. Gets a sequential-MAC pair bonus if a `:DE`/`:DF` adjacent-channel pair is seen (`checkSeqMac()`). |
| `ALERT_OUI_MFR`       | `addr2` matches a contract-manufacturer OUI (Liteon/USI, incl. `14:b5:cd`) shared with non-Flock devices | `oui_mfr` | Lower confidence (`CS_OUI_MFR=20` < `CHIRP_MIN_CONFIDENCE=30`) — logged silently, no chirp/LED alone. Liteon OUIs live in this tier even when the community dataset lists them flat (`14:b5:cd`): the silicon ships in unrelated consumer gear, so HIGH would reintroduce the `f8:a2:d6` false-positive class. |
| `ALERT_SOUNDTHINKING` | `addr2` matches the SoundThinking/ShotSpotter acoustic-sensor OUI          | `soundthinking`    | Often co-deployed with Flock cameras; `CS_SOUNDTHINKING=35` does chirp |

Sequential-MAC bonus: two wildcard-probe hits from the same OUI prefix
with suffix bytes `:DE` then `:DF` on adjacent channels within a short
window get a confidence bonus (`applySeqMacBonus()`, tracked in
`seqMacTable[]`, size `SEQ_MAC_TABLE_SIZE`).

IE-fingerprint bonus (`fyCheckFlockIeSignature()` in `fy_detect.h` →
`applyIeSigBonus()` in `fy_confidence.h`): the Probe Request's raw IE TLVs are
walked in order and encoded as a signature string — SSID skipped, vendor IE 221
as `"221:"` + 8 payload bytes hex, every other IE as its decimal tag — then
compared against the drive-tested allowlist
`2,12,127,221:506f9a16030103,45,191,221:0050f208000000` (upstream
colonelpanichacks/flock-you). A match adds `CS_IE_SIG_BONUS=18` (62 → 80) —
**high-tier OUIs only**. Applying it to an mfr-tier hit would lift 20 → 38, past
`CHIRP_MIN_CONFIDENCE` (30), which is precisely the false-positive class the mfr
tier exists to prevent; that combination previously made status LEDs appear
permanently stuck red. The `iesig=` counter still counts every fingerprint match
(including mfr-tier), so its prevalence on shared Liteon/USI hardware stays
visible — if `iesig` climbs on mfr-tier traffic, the fingerprint is too generic
to keep.
Two deliberate design points:

- **Additive, never a gate.** Upstream *replaced* its wildcard-probe check with
  this signature and justified disabling its addr1/addr3 tiers on the back of
  it. This build does not: a camera on firmware we have not fingerprinted must
  stay detectable, so a non-match still enqueues at 62. `DETECTION_IMPROVEMENTS.md`
  §7 reaches the same conclusion. The allowlist is an array specifically so a
  second, firmware-derived fingerprint can be **added** later, not swapped in.
- **Tolerant parsing on purpose.** ESP32 promiscuous captures are routinely
  truncated or skewed, so the walk accepts a phantom tag-64/len-128 overflow,
  resyncs forward to the next plausible TLV header, and retries from three
  spans: full body, `body+2` (no leading empty-SSID IE), and body minus the
  trailing 4-byte FCS. Counter: `iesig=` in the `stats gate` line — flat `iesig`
  with rising `wild` means probes arrive but the fingerprint does not match.

## BLE detections (`fyProcessBLEAdvertisedDevice()` in `main.cpp`, only
when `ENABLE_BLE_SCAN=1`)

| AlertType             | Trigger                                                          | Method string     | Confidence |
|-----------------------|--------------------------------------------------------------------|-------------------|------------|
| `ALERT_BLE_MFR_ID`    | Manufacturer-specific data with company ID `0x09C8` (XUNTONG/Flock) | `ble_mfr_id`      | `CS_BLE_MFR_ID_STANDALONE=45` (+5 if RSSI > -70) |
| `ALERT_BLE_RAVEN_UUID`| Advertised service UUID matches one of the 5 **named** `fy_raven_uuids[]` services (GainSec-documented) | `ble_raven_uuid` | `CS_BLE_UUID_STANDALONE=45` |
| `ALERT_BLE_RAVEN_RANGE`| Advertised 16-bit service is inside `0x3100`–`0x3500` but is **not** one of the named services | `ble_raven_range` | `CS_BLE_UUID_RANGE_STANDALONE=20` — **below `CHIRP_MIN_CONFIDENCE`, so silent.** See the note below. |
| `ALERT_BLE_FLOCK_GATT`| Advertised service UUID matches `fy_ble_gatt_uuids[]` — the Flock accessory service `e8ccbb38-9532-46a8-9fe5-1814df172e6f` or the Nordic legacy DFU service `00001530-1212-efde-1523-785feabcd123` | `ble_flock_gatt` | `CS_BLE_GATT_STANDALONE=45` |
| `ALERT_BLE_NAME`      | Device name matches `fyCheckFlockBleName()`: a substring keyword from `fy_ble_names[]` (`FS Ext Battery`, `Penguin`, `Flock`, `Pigvision`, `Raven`, `DfuTarg`) **or** a shape from `fyCheckBleNamePattern()` (`Penguin-` + 10 digits, a bare 10-digit serial, `FS Ext Battery`, `DfuTarg`) | `ble_name` | `CS_BLE_NAME_STANDALONE=35` |

Notes on the firmware-derived BLE additions (2026-09-16 dump):

- **Standard Bluetooth SIG services must never alert standalone.** `0x180A`
  (Device Information), `0x1809` (Health Thermometer) and `0x1819` (Location and
  Navigation) used to sit in `fy_raven_uuids[]` because GainSec lists them; they
  are advertised by essentially every BLE device made, so at
  `CS_BLE_UUID_STANDALONE=45` (above the chirp threshold) an ordinary fitness
  band alerted as a "Raven camera". They now live in
  `fy_raven_legacy_uuids[]` as **firmware-estimation evidence only**, and
  `fyService16IsStandardSvc()` is consulted by the matcher so that re-adding one
  to the alert table cannot resurrect the bug. Two tests enforce this
  (`test_raven_uuid_standard_services_never_alert`,
  `test_raven_table_has_no_standard_services`). Note the deliberate
  **test-behaviour change**: the old `test_raven_uuid_known_device_info`
  asserted 0x180A matched positively.
- **The Raven `0x3100`–`0x3500` range is matched, but scored as a WEAK tier.**
  The named list only holds the round hundred values, while the services that
  leak GPS (`0x3101`/`0x3102`) sit between them — so the range must be matched or
  those are missed entirely. But scoring an in-range match as a standalone Raven
  camera (45) produced a **live false positive (2026-09-21)**: an unnamed device
  with a randomised MAC at −88 dBm chirped and held the alert LED red, logged as
  `ble_raven_uuid`, when the only thing "Raven" about it was that its service
  UUID happened to land inside an unassigned 1025-value block. `ALERT_BLE_RAVEN_RANGE`
  (`ble_raven_range`, `CS_BLE_UUID_RANGE_STANDALONE`=20) now records those below
  the chirp threshold, so they still appear in the dashboard, the JSON and
  `stats ble … ravenrange=` — they just cannot alert on their own. Only the 5
  named services may alert stand-alone. If `ravenrange=` ever turns out to be
  dominated by genuine Raven devices, revisit; if the *named* 5 ever false-fire
  the same way, they need the same treatment.
  `fyClassifyRavenUUIDFromStrings()` (FY_RAVEN_MATCH_NAMED / _RANGE / _NONE) is
  what distinguishes them; `fyCheckRavenUUIDFromStrings()` stays as the
  any-kind wrapper so existing callers/tests keep their meaning.
  `fyService16FromUuidString()` parses both the
  the canonical 128-bit form NimBLE emits and the short `0x3101`/`3101` forms.
  (Intended side effect: a few **existing tests changed behaviour**, e.g. the
  old "short-form UUIDs never match" test still passes because `1b7e`/`fd60` sit
  outside the range.)
- The Flock accessory service deliberately does **not** share the Raven alert
  type: labelling Flock's own GATT service `ble_raven_uuid` would mislabel it on
  the dashboard and in CSV export.
- `ALERT_BLE_NAME`'s advertised name is carried through `AlertEntry.ssid` and
  emitted as `device_name` in the JSON (and in the `DETECT-BLE` log line). It is
  *not* written into the detection table's `ssid` field, because that field is
  persisted/exported as an SSID and a device name there would read as an SSID
  match.

These are **standalone** alerts — a BLE-only match produces a real alert
immediately (no corroborating WiFi frame required). This was a
significant historical bug: BLE matches used to only set a timestamp for
a later WiFi-hit confidence *bonus* (`CS_BLE_CORR`, `BLE_CORR_WINDOW_MS`)
and never called `enqueueAlert()` on their own — meaning BLE-only Flock
signals were completely invisible (no LED, chirp, log line, or JSON).
Fixed; see `git log` for `fyProcessBLEAdvertisedDevice()`.

`BLE_COEX_MODE=1` (used by every `-ble` PlatformIO environment) runs
WiFi promiscuous mode and a continuous NimBLE scan simultaneously via the
ESP-IDF software coexistence scheduler, rather than time-multiplexing
(pausing WiFi to run BLE scans). See `bleCoexStart()`/`bleScanTick()`.

## Confidence scoring

All weights/thresholds live in `fy_confidence.h`
(`computeConfidence()`), with `CHIRP_MIN_CONFIDENCE` (currently 30) as the
audible/visual-alert threshold — detections below it are logged but
silent, letting low-confidence signals (e.g. `ALERT_OUI_MFR`) be recorded
without generating alert fatigue.

## Test tooling that exercises these paths

- `ble_selftest.h` (`BLE_SELF_TEST=1`, `m5atom-lite-ble-selftest` env):
  single board self-advertises the 3 BLE scenarios and picks them back up
  via its own always-on coex scan.
- `beacon_test.cpp` (`m5atom-lite-beacon` env, separate standalone
  firmware): broadcasts all 17 scenarios (12 WiFi + 5 BLE, 1:1 with the
  tables above) on a rotating schedule, for testing against a SECOND board
  running the real detector — the preferred test method since it doesn't
  depend on same-radio self-reception quirks. Scenarios 12–14 cover the
  firmware-derived additions specifically (exact default-MAC, Flock
  accessory GATT service, and a bare-serial BLE name that only the shape
  matcher can catch), scenario 15 fires the IE-signature probe (same method as
  scenario 1 but conf 80 instead of 62 — comparing the two *is* the test), and
  scenario 16 carries the CVE-2025-59409 `test_flck` SSID, and each derives its
  payload from `fy_detect.h`'s
  tables rather than re-hardcoding it. Each WiFi scenario is sent
  via `txSweep()`, which repeats the {1,6,11} channel sweep
  `SWEEP_PASSES` times (currently 6, ~576 ms total burst) so a single
  scenario firing is long enough to overlap a real detector's channel-hop
  dwell window — see "WiFi channel hopping & channel lock" below for why
  this matters. `txSweep()` also **counts and reports** every failed
  `esp_wifi_80211_tx()`/`esp_wifi_set_channel()` call (rate-limited to one
  `[beacon] WARN ...` line per second) instead of ignoring the return
  values — until this was added the tester could not distinguish "the
  detector missed my frames" from "the driver never put my frames on the
  air at all", which is one of the two possibilities the diagnosis below
  has to rule out.

## Arrival-vs-match diagnostics (`FY_SNIFF_STATS` in `main.cpp`)

`main.cpp` carries cheap, always-on diagnostic counters (compiled out by
setting `FY_SNIFF_STATS` to 0) reported as extra `[flockyou] stats ...`
lines by the 30 s heartbeat in `printHeartbeat()`. They exist specifically
to answer the question the static code reviews could not: **did a frame
physically arrive at the radio, or did it arrive and fail to match?**

- **ARRIVAL counters** (`rx`/`badlen`/`weakrssi`/`mgmt`/`data`/
  `beacon`/`preq`/`presp`/`other`/`ch1`/`ch6`/`ch11`) are incremented at
  the top of `wifiSniffer()` **before any OUI/SSID table is consulted**,
  so they cannot be affected by matching logic:
  - `rx=0` during a scenario burst → frames never reached the callback
    (RF/timing, TX refused by the driver, or wrong channel) — check the
    tester's `[beacon] WARN` lines too.
  - frames arriving **only** on the wrong `chN` → the dwell/overlap
    timing issue described under "WiFi channel hopping".
  - `weakrssi` climbing → the frames arrived but were below `RSSI_MIN`.
  - `badlen` climbing → truncated/malformed captures.
- **GATE counters** (`gate a2/wild/a1/a3/ssid/laa/seqpair`) are
  incremented at the exact point each gate's final condition passes,
  immediately before its `enqueueAlert()` call:
  - arrival counts non-zero **and** gate count zero → the frame arrived
    but failed to match (look at the pattern table / scenario MAC
    construction, e.g. `pickRandomOuiGA()`).
  - gate count non-zero **and** no `DETECT-*` line → the alert was
    enqueued but lost downstream (check the queue counters).
- **QUEUE counters** (`queue ok/drop/drained`) close the last gap:
  `enqueueAlert()` returns silently when the 32-slot ring buffer is full,
  which was previously an invisible way for a `matched` detection to
  vanish before it could be logged. `drop` must stay 0; a rising `drop`
  means the alert rate outran `loop()`'s drain.
- **`stats ble adv/mfr/uuid/name`** is the BLE equivalent: `adv` counts
  every advertisement handed to `fyProcessBLEAdvertisedDevice()` (before
  matching). `adv=0` while the tester is advertising means the
  advertisements never reached the scanner (NimBLE scan window/interval
  mismatch), not a matching failure.

## WiFi channel hopping & channel lock

The WiFi radio can only listen on one 2.4 GHz channel at a time, so
`updateChannelMode()` hops the promiscuous-mode channel on a timer
(`CHANNEL_DWELL_MS`, currently 100 ms per channel) across `{11, 6, 1}`
(Flock's observed primaries), giving a full rotation period of ~300 ms.
`CHANNEL_DWELL_MS` was previously 250 ms (750 ms/rotation) — lowered
after cross-device `beacon_test.cpp` testing showed short-lived bursts
could land entirely within a dwell window on the *wrong* channel and be
missed for that hop cycle with no second chance. Any transmitter whose
signal duration is shorter than a full rotation period risks being missed
purely due to this timing, independent of matching-logic correctness —
this is why `beacon_test.cpp`'s `SWEEP_PASSES` was also raised (see
above): the tester's burst duration must exceed the detector's rotation
period for a reliable test.

**Channel lock** (`maybeLockChannel()`, called from `drainAlertQueue()`):
once a chirp-worthy WiFi detection fires (`confidence >=
CHIRP_MIN_CONFIDENCE`), the detector stops hopping and locks
`currentChannel` to the exact channel the hit came in on
(`channelLockActive = true`), so it can keep receiving frames from a
camera we KNOW is live right now instead of spending 2/3 of its time on
channels with nothing confirmed. The lock releases automatically
(`updateChannelMode()`) after `CHANNEL_LOCK_TIMEOUT_MS` (5000 ms) of no
fresh qualifying hit on that channel, resuming normal hop. BLE detections
never trigger or interact with channel lock — `e.channel` is meaningless
for BLE alerts (`ALERT_BLE_MFR_ID`/`ALERT_BLE_RAVEN_UUID`/`ALERT_BLE_NAME`
are explicitly excluded in `maybeLockChannel()`), and `BLE_COEX_MODE`'s
NimBLE scan runs independently of `currentChannel` regardless.

Hardware-validated (two-board cross-device test, `m5atom-lite-beacon` →
`m5atom-lite-ble`): channel lock engages/releases correctly with clean
5-second-timeout cycles and no freezes/reboots observed across two
independent ~90-second runs.

## Known open issue: some alert types under-detected in cross-device testing

During the same two-board hardware validation runs referenced above, the
following alert types were **never** caught despite `beacon_test.cpp`
firing their scenarios repeatedly (0 hits out of ~17 combined fires
across both runs): `ALERT_OUI_ADDR1`, `ALERT_OUI_ADDR2`, `ALERT_OUI_ADDR3`,
`ALERT_LAA_SSID`, the `SEQ_MAC_PAIR_BONUS` on top of
`ALERT_WILDCARD_PROBE`, `ALERT_BLE_RAVEN_UUID`, and `ALERT_BLE_NAME`. In
the same runs, `ALERT_WILDCARD_PROBE`, `ALERT_SSID`, `ALERT_OUI_MFR`,
`ALERT_SOUNDTHINKING`, and `ALERT_BLE_MFR_ID` were all reliably caught
using structurally similar code paths.

Two independent full code-review passes (across separate sessions) of
`wifiSniffer()`'s addr1/addr2/addr3/SSID gating, `fyProcessBLEAdvertisedDevice()`'s
mfr-ID/UUID/name matching, `fy_confidence.h`'s OUI/sequential-MAC logic,
and `beacon_test.cpp`'s/`beacon_frames.h`'s frame construction (confirmed
byte-layout-compatible with `wifiSniffer()`'s parsing offsets) **found no
coding bug** that would explain this specific pattern. The channel-lock
feature was also specifically checked as a possible new cause and ruled
out, since `txSweep()` always sweeps all three channels regardless of
which channel the detector is currently dwelling/locked on.

This remains an **open, unresolved limitation** as of this writing. It is
flagged here rather than silently left for a future session to
re-discover from scratch. Suspected candidate causes not yet
instrumented/tested:
- BLE scenarios (`ALERT_BLE_RAVEN_UUID`, `ALERT_BLE_NAME`): possible NimBLE
  scan-window/advertisement-interval timing mismatch analogous to the
  WiFi dwell-vs-burst issue above, or an issue specific to
  `bleAdvertiseAndHold()`'s advertisement parameters not reliably
  reaching the scanner during its active NimBLE scan window.
- WiFi addr1/addr3/LAA-SSID/seq-mac scenarios: possible RSSI or antenna
  orientation effects specific to those frame subtypes/scenarios, an
  as-yet-unfound edge case in `beacon_test.cpp`'s `pickRandomOuiGA()` or
  the hardcoded LAA MAC/SSID in scenario5, or a real-world timing
  interaction not reproduced by static code review.
- **Instrumentation is now in place** (this replaces the earlier
  "recommended next diagnostic step"): the `FY_SNIFF_STATS` counters in
  `main.cpp` plus `beacon_test.cpp`'s `[beacon] WARN` TX-error reporting
  (both documented above) separate these cases directly. Procedure for the
  next hardware session:
  1. Flash `m5atom-lite-beacon` to board A and a `-ble` detector build
     (e.g. `m5atom-lite-ble`) to board B; capture B's serial output from
     boot onward for at least 2-3 minutes so several 30 s heartbeat
     windows and every scenario in A's rotation are covered.
  2. For each scenario A reports firing, compare against B's next `stats`
     lines: growth in `rx`/`presp`/`preq` (or `stats ble adv`) proves the
     frames arrived; a *flat* corresponding `gate` counter alongside
     arrival growth proves a matching failure; a rising `queue drop`
     proves the alert was enqueued and lost downstream.
  3. Cross-check A's log for `[beacon] WARN` lines in the same window. If
     the driver refused the injection then B's arrival counters stay flat
     and the fault is on the **tester** side, not in `wifiSniffer()`/
     `fyProcessBLEAdvertisedDevice()`.
  Status: **not yet interpreted from a hardware capture** — the counters
  were added with no board attached, so they are build-verified only.
  Treat the candidate causes below as still-open until a capture says
  otherwise.
