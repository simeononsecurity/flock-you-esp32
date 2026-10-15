---
name: flock-you-dashboard-wire
description: "Maintain the Flock-You serial JSON contract, Flask dashboard, detection persistence, imports, exports, GPS correlation, and dashboard UI."
---

## Read first

Read `.clinerules/01-clean-code.md`, the applicable verification rules in `.clinerules/02-test-before-commit.md`, and `docs/adr/0002-serial-schema-versioning.md`. Inspect `main.cpp`'s JSON emitter, `api/flockyou.py`, `api/templates/index.html`, and `api/README.md` before editing.

## Preserve the wire contract

Treat serial JSON as a public API. Adding an optional field keeps the current schema. Renaming, removing, or changing the meaning or type of an existing field requires a schema bump, matching boot-banner update, consumer handling, changelog entry, and documentation update.

Trace each field through emission, `json.loads`, normalization, deduplication, cumulative storage, Socket.IO, dashboard rendering, CSV export, KML export, and JSON or CSV import. Reject silent drops. Preserve unknown optional fields where the current path already does so. Warn once when a device reports a newer schema.

Keep protocol fields distinct:

- `protocol` identifies WiFi or BLE.
- `band` records the radio band.
- `detection_method` records the stable method string.
- `ssid` stays a WiFi SSID.
- `device_name` stays a BLE advertised name.
- `matched_signatures` and `firmware_sig` describe firmware-derived corroboration.

Do not treat regex logs as equivalent to structured JSON. Keep legacy parsing only where a supported input still needs it. Avoid blocking serial and GPS readers from a request handler. Keep shared state updates synchronized with the current threading model.

## Verify

Syntax-check Python and JavaScript. Feed synthetic current-schema, older-schema, newer-schema, WiFi, BLE, malformed, duplicate, import, and export records through the real consumer functions. Exercise Flask endpoints with a test client and inspect rendered or returned data. Keep fixtures outside tracked data unless the request asks for tests.

For firmware emitter changes, run affected builds. For live serial claims, capture a real detection or a beacon-test record. Report schema compatibility, fields tested, endpoint checks, and any hardware gap.
