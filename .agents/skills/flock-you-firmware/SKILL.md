---
name: flock-you-firmware
description: "Implement or review Flock-You ESP32 firmware, board support, GPIO, audio, display, radio coexistence, and PlatformIO environment changes. Use the detection skill for signature or confidence work."
---

## Read first

Read `.clinerules/01-clean-code.md`, `.clinerules/02-test-before-commit.md`, and `.clinerules/03-file-size-and-decomposition.md`. Inspect `platformio.ini`, the affected board headers, and the matching CI and flasher entries before editing.

## Scope the change

Identify the chip family, board environment, BLE mode, display path, audio path, USB transport, flash size, partition table, and reserved pins. Treat M5Atom Echo and M5Atom Voice as one physical product with different firmware paths. Treat VoiceS3R native USB and ESP32-C5 support as separate platform cases.

Keep production code out of test firmware. Put separable features in focused headers instead of growing `main.cpp`. Follow the file's current naming prefix. Keep ISR, promiscuous callback, and BLE host callback work allocation-free and logging-free. Send events through `enqueueAlert()` and drain them from the main loop.

Do not rely on `sdkconfig.defaults` to change precompiled Arduino framework libraries. Prove boot-time settings through observable output or binary inspection. Initialize M5Unified audio explicitly and log board identity plus initialization failure.

When adding a user-flashable board or variant, keep these surfaces aligned:

- `platformio.ini`
- board definition JSON, when needed
- `.github/workflows/build-firmware.yml`
- `docs/index.html` board and capability data
- flash scripts, only when transport or reset behavior differs
- README support tables

## Verify

Run native tests when shared headers change. Build every environment affected by shared code, including BLE and non-BLE siblings. Rebuild after a suspicious cache hit. A clean compile supports a build claim only. Runtime, audio, display, radio, boot, and reset claims require serial or visible hardware evidence from the named board.

Review warnings, not only errors. New volatile counters must avoid deprecated increment operators. Re-read the diff and update `.clinerules/` when the work proves a durable new hardware or framework rule.
