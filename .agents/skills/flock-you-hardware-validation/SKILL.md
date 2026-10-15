---
name: flock-you-hardware-validation
description: "Build, flash, monitor, and diagnose Flock-You devices on physical ESP32, ESP32-S3, and ESP32-C5 hardware. Use for live radio, boot, reset, audio, display, and two-board validation."
---

## Read first

Read `.clinerules/02-test-before-commit.md` and the affected board block in `platformio.ini`. Read `.clinerules/04-detection-methods.md` for radio or alert-pipeline work. Resolve each attached serial port to an exact board before flashing.

## Build and flash

Build the named environment. Clean and rebuild when source changed but the result looks cached. Treat the first fresh ESP32-C5 platform-install failure as provisional and retry once when its log matches the documented package-install failure.

Do not flash an unresolved port or replace working firmware outside the requested device scope. Record the environment, port, firmware hash or build time, upload result, and reset method.

For VoiceS3R native USB, port presence and successful esptool output do not prove application boot. Require a `[flockyou]` boot or heartbeat line. If the board stays in the ROM loader after a verified flash, report the documented physical unplug and replug step instead of claiming success.

## Capture evidence

Capture from boot when the symptom involves startup, reset, or silence. Inspect the ROM reset reason before labeling a crash. Avoid reopen loops on native USB because opening the port itself triggers `USB_UART_CHIP_RESET`. Use a passive monitor or `stty` plus `cat` where appropriate.

For RF validation, prefer two boards:

- Board A runs `m5atom-lite-beacon`.
- Board B runs the requested production `-ble` detector environment.
- Capture at least several full scenario rotations and heartbeat windows.

Correlate the tester's scenario lines and TX warnings with detector counters. Arrival growth with a flat gate identifies matching logic. Flat arrival with tester success points to RF, channel, or timing. Rising queue drops identify downstream loss. Require repeated evidence before calling an intermittent radio issue fixed.

## Report

Separate build results, upload results, application boot, live runtime behavior, and untested claims. Include the exact board, environment, port, duration, reset reason, heartbeat evidence, and detection counts. Do not convert missing hardware access into a runtime pass.
