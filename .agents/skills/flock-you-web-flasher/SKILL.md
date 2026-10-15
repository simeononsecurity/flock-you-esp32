---
name: flock-you-web-flasher
description: "Change or diagnose the Flock-You static web flasher, firmware manifests, board capability UI, runtime config blob, and CI artifact publishing."
---

## Read first

Read the web-flasher sections in `.clinerules/02-test-before-commit.md` and `.clinerules/03-file-size-and-decomposition.md`. Read `docs/adr/0001-config-partition-and-flasher-configurator.md` for config work. Inspect `docs/index.html`, `fy_config.h`, `test/test_config/test_config.cpp`, `partitions_4mb.csv`, `platformio.ini`, and `.github/workflows/build-firmware.yml` as applicable.

## Preserve the release matrix

Keep each distinct user-flashable environment aligned across PlatformIO, CI build lists, artifact checks, copied binaries, generated manifests, flasher `BOARDS` data, manifest-to-board resolution, and `BOARD_CAPS`. Default production board choices to BLE while retaining the WiFi-only sibling. Keep self-test and beacon builds labeled as test firmware. Keep experimental C5 builds advisory.

Match chip family and offsets exactly. Classic ESP32 uses the classic bootloader offset. ESP32-S3 and ESP32-C5 use their defined offsets. Do not infer success from a manifest link alone. Verify every referenced environment produces each required binary.

## Config blob contract

Treat `fy_config.h` as the firmware contract. Keep magic, version, length, offsets, little-endian fields, reserved bits, engine bits, clamps, and IEEE CRC-32 synchronized with JavaScript. Engine bits must stay unique and below 32. A new schema version requires compatible fallback behavior.

The web page uses large inline scripts. Exercise the shipped script rather than a rewritten copy:

- Parse every inline script.
- Confirm expected functions and listeners exist at top level.
- Dispatch config interactions through a stub DOM or browser.
- Resolve every named manifest through board ID and capability lookup.
- Generate a config blob in JavaScript and decode it with the real C header.

## Verify

Run `pio test -e native` for config changes. Build affected firmware environments. Inspect the page in a browser for board selection, capability controls, firmware variants, config-only flashing, serial monitor behavior, and stale object URLs or duplicate listeners. Treat successful HTML loading as weaker evidence than a completed interaction.

Report source checks, interaction checks, binary-contract checks, and build coverage separately. Deployment to GitHub Pages stays outside a source-edit request unless the user includes deployment.
