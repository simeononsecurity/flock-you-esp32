---
name: flock-you-detections
description: "Add, correct, or review Flock-You WiFi and BLE signatures, alert types, confidence scoring, beacon scenarios, and detection tests. Excludes generic board support and dashboard-only changes."
---

## Read first

Read `.clinerules/01-clean-code.md`, `.clinerules/04-detection-methods.md`, and `.clinerules/05-keep-rules-current.md`. Inspect the source dataset or research record, then inspect `fy_detect.h`, `fy_confidence.h`, the matching `main.cpp` path, beacon tooling, and native tests.

## Evidence and design

Record the exact signal, protocol, provenance, expected prevalence, false-positive class, and whether the signal identifies Flock equipment or only supports correlation. Do not promote a shared manufacturer, standard Bluetooth service, or broad UUID range into an alerting tier without field evidence.

Keep one source of truth:

- Match data and shape rules belong in `fy_detect.h`.
- Weights and thresholds belong in `fy_confidence.h`.
- `main.cpp` parses frames, calls matchers, enqueues alerts, and emits results.
- Test transmitters derive payloads from shared tables where possible.

Before changing a weight, calculate the resulting score for every tier reached by the matcher. Preserve the quiet contract-manufacturer and Raven-range tiers below `CHIRP_MIN_CONFIDENCE`. Keep the runtime config threshold floor from weakening the compile-time safety floor.

For a new alert path, trace the complete contract: enum, method string, engine-mask bit, queue entry, confidence computation, JSON, session persistence, dashboard handling, stats counter, tester scenario, and documentation. Preserve `device_name` versus `ssid` semantics for BLE.

## Verify

Add positive, negative, boundary, case, and near-miss tests. Run `pio test -e native`. Build every environment whose compile guards include the change. Use `m5atom-lite-beacon` against a production detector for RF claims. Compare tester warnings, arrival counters, gate counters, queue counters, and emitted detection lines. A matcher unit test does not establish over-the-air reception.

Update `.clinerules/04-detection-methods.md` whenever a path or score changes. Update public detection tables, changelog entries, and firmware-derived provenance when users would observe the change.
