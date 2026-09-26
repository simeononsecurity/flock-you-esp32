# ADR-0002: Version the serial/JSON wire format

**Status:** Accepted
**Date:** 2026-09-19

## Context

The firmware's serial output is not just a debug log — it is the **API** between
the device and its consumers:

- `api/flockyou.py` parses the JSON detection lines (`json.loads` per line) and
  feeds the Flask dashboard, sessions and CSV export.
- Users now capture those lines themselves, and the printable guides and
  `docs/customer-replies.md` tell them what the fields mean.
- The beacon tester (`beacon_test.cpp`) and the self-test build mirror the same
  field names deliberately, so the serial and JSON shapes are documented
  behaviour, not incidental.

There is no version marker anywhere in that stream, so a consumer cannot tell
which format a unit speaks, and a change to field names or semantics breaks
consumers **silently**. This project has already lost detections exactly that
way three times: the SSID lines and the padded right-aligned OUI lines failed to
match the dashboard's regexes, and the Raven UUID line printed a single space
where the pattern required two — every one of those returned nothing, raised
nothing, and simply never appeared on the dashboard.

## Decision

**Put an integer schema version in the emitted stream, and treat any change to
the shape or meaning of existing fields as requiring a bump.**

- Every detection JSON gains `"schema": <n>` (currently `1`).
- The boot banner gains a matching `schema=<n>` token, so a unit can be
  identified before any detection occurs.
- Adding a *new optional* field does not bump the version; changing or removing
  a field, or changing what an existing value means, does.

## Consequences

**Gains**

- A consumer can detect a mismatch instead of silently dropping data. The
  dashboard can log "unit speaks schema 2, this API understands 1" rather than
  recording half a detection.
- Support can ask a user to paste the boot banner and know immediately which
  build and which format they have — the same reason the banner already prints
  board, OUI counts and channel mode.
- Adding the field is backward compatible for every current consumer: the Flask
  API parses with `json.loads`, so an extra key is ignored by older code, and
  the CSV exporter selects columns explicitly.

**Costs**

- Discipline: a version that nobody bumps is worse than none, because it implies
  a guarantee that does not hold. Any change to an existing field must bump it
  and must say so in `CHANGELOG.md`.
- One more field in every JSON line (14 bytes/line). Irrelevant at this line
  rate, and it is compiled out only if the whole JSON emitter is.
- The version is *not* self-enforcing — a consumer that ignores the field gains
  nothing. It is a contract, not a mechanism.

## Alternatives considered

- **No version, rely on git history** — rejected: consumers are end users and
  forks that never see our git history, and the three silent-drop bugs above
  prove that "obviously compatible" changes are not.
- **Version the manifest/document with a separate file** — rejected: the
  firmware cannot read a file on the consumer's machine, and the stream is
  exactly where the ambiguity shows up.
- **Semantic versioning of the whole firmware only** — rejected as insufficient:
  the same firmware version can emit different shapes across a fork, and a
  consumer needs to know what it is *reading*, not what it is talking to.
