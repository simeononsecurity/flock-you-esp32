# Flock-You ESP32 - Complete Build Package

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Author](https://img.shields.io/badge/Author-SimeonOnSecurity-green.svg)](https://github.com/simeononsecurity)

**WiFi promiscuous-mode detector for Flock Safety surveillance cameras**

Ported to standard ESP32 hardware for maximum accessibility and cost savings.

---

## 🚀 Quick Links

- **[Solderless Build Guide](SOLDERLESS_BUILD_GUIDE.md)** - No soldering required! ($9-11 total)
- **[PCB Design Package](hardware/pcb/README.md)** - Custom board: schematic, BOM, assembly
- **[Detection Methods](.clinerules/04-detection-methods.md)** - Every detection path, with scoring and test tooling

---

## ✨ What's Included

This package contains everything you need to build and deploy your own Flock-You detector:

### 📁 Firmware (repo root)
- **main.cpp** - Modified for ESP32 (GPIO 25, 2, 17)
- **platformio.ini** - ESP32 DevKit configuration
- **partitions_4mb.csv** - Optimized for 4MB flash
- **api/** - Flask dashboard for GPS wardriving
- **datasets/** - OUI lists & research data, including
  [firmware_derived_signatures.md](datasets/firmware_derived_signatures.md)
  (the signatures extracted from a real Flock camera firmware image, with the
  constant that holds each one)

### 🔧 Hardware (`/hardware`)
- **pcb/** - Custom PCB design package (schematic, BOM, assembly guide)

**Note:** there is no published case design — see
[hardware/README.md](hardware/README.md) for why.

### 📚 Documentation
- **[Solderless Build Guide](SOLDERLESS_BUILD_GUIDE.md)** - Assembly, testing, troubleshooting
- **[PCB Design Package](hardware/pcb/README.md)** - Schematic, BOM, assembly guide
- **[Detection Methods](.clinerules/04-detection-methods.md)** - Detection paths and confidence scoring
- **[Firmware-Derived Signatures](datasets/firmware_derived_signatures.md)** - Signature provenance
- **[Printable Quick Start](docs/print/quick-start-4x6.pdf)** - Plain-language pocket card for
  non-technical users ([8.5x11 sheet version](docs/print/quick-start-letter.pdf))

---

## 💰 Cost Breakdown

| Build Type | Components | Total Cost | Detection Accuracy |
|------------|------------|------------|-------------------|
| **Minimal** | ESP32 + USB cable | **$5** | ✅ 100% |
| **Breadboard** | + Buzzer + breadboard | **$9-11** | ✅ 100% |
| **OUI-SPY** | Pre-built board | **$85** | ✅ 100% |

**Same detection performance, 85% cost savings!**

---

## 🎯 Two Ways to Build

### Option 1: LED-Only (Cheapest)
**Cost:** $5 | **Time:** 5 minutes | **Difficulty:** ⭐☆☆☆☆

- ESP32 DevKit + USB cable
- Onboard LED provides visual feedback
- Perfect for testing or silent operation

### Option 2: Breadboard Build (Recommended)
**Cost:** $9-11 | **Time:** 10 minutes | **Difficulty:** ⭐⭐☆☆☆

- Add passive buzzer module + breadboard
- Audio chirps on detection
- No soldering required
- [Full Guide](SOLDERLESS_BUILD_GUIDE.md)

---

## 🛠️ Quick Start

### 1. Get Hardware
**Minimum:**
- [ESP32 DevKit](https://amazon.com/s?k=ESP32+DevKit) ($5-6)
- USB Micro cable ($1)

**Recommended:**
- [ESP32 Breadboard Kit](https://amazon.com/s?k=ESP32+breadboard+kit) ($15-20)
- Includes everything: ESP32 + breadboard + jumpers + buzzer

### 2. Flash Firmware
```bash
# Install PlatformIO
pip install platformio

# Clone / enter the repo
cd flock-you-esp32

# WiFi-only (recommended first flash — works on any ESP32 DevKit)
pio run -e esp32dev -t upload && pio device monitor

# WiFi + BLE coexistence (continuous BLE scan + WiFi simultaneously)
pio run -e esp32dev-ble -t upload && pio device monitor

# M5Atom variants — use the unified flasher script
./flash.sh          # interactively identifies your device
./flash.sh --once   # flash one device and exit
```

**All supported environments:**

| Environment | Board | BLE |
|-------------|-------|-----|
| `esp32dev` | ESP32 DevKit | — |
| `esp32dev-ble` | ESP32 DevKit | ✅ COEX |
| `m5atom-lite` | M5Atom Lite | — |
| `m5atom-lite-ble` | M5Atom Lite | ✅ COEX |
| `m5atom-echo` | M5Atom Echo | — |
| `m5atom-echo-ble` | M5Atom Echo | ✅ COEX |
| `m5atom-voice` | M5Atom Voice | — |
| `m5atom-voice-ble` | M5Atom Voice | ✅ COEX |
| `m5atom-voices3r` | Atom VoiceS3R (S3) | — |
| `m5atom-voices3r-ble` | Atom VoiceS3R (S3) | ✅ COEX |
| `lilygo-t-dongle-c5` | LILYGO T-Dongle C5 | — |
| `lilygo-t-dongle-c5-ble` | LILYGO T-Dongle C5 | ✅ NimBLE 2.x |

### 3. Test Detection
- Device boots with Super Mario 1-2 startup tune
- LED flashes on WiFi traffic
- Buzzer chirps on Flock camera detection
- Drive near known camera locations to verify

**No camera nearby? Use the built-in beacon tester.** Flash `m5atom-lite-beacon`
(from the web flasher, or `pio run -e m5atom-lite-beacon -t upload`) to a
*second* board and leave it powered near your detector. It broadcasts all **15
test scenarios** — one per detection path, including the firmware-derived ones
(exact factory-default MAC, Flock accessory GATT service, bare-serial BLE name)
— on a rotating schedule. Each scenario derives its payload from `fy_detect.h`,
so the tester cannot drift out of sync with the detector's tables.

If a scenario is not detected, check the **tester's** `[beacon] WARN` lines
first (a driver-refused transmission looks exactly like a detector miss), then
the **detector's** `[flockyou] stats …` counters from the 30 s heartbeat — they
separate "the frame never arrived" from "it arrived and failed to match". See
`.clinerules/04-detection-methods.md` for how to read them.

`m5atom-lite-ble-selftest` is the single-board alternative: it advertises the
fake Flock BLE signals to itself and picks them back up with its own coex scan.
It is a **test build — never use it as a real detector.**

**That's it!** You're detecting.

---

## 📊 Detection Methodology

This firmware uses **five research-proven techniques** with a confidence score (0–100):

### 1. WiFi Promiscuous Sniffing (@NitekryDPaul + firmware-derived)
- Monitors 2.4 GHz management & data frames
- **Four OUI confidence tiers** (PR#39 + firmware-derived set):
  - **HIGH** (33 OUIs) — exclusively Flock Safety registered → score 40, always alerts
  - **MFR** (7 OUIs) — Liteon/USI contract manufacturer **+ `00:03:7f` Qualcomm Atheros (the camera's QCA9377 radio)** → score 20, silent log only
  - **SoundThinking** (1 OUI) — acoustic sensor co-deployed with Flock → score 35, alerts
  - **FW-default MAC** (2 full addresses) — `00:03:7f:50:00:01` / `00:03:7f:4f:00:16`, the **factory-default** QCA9377 radio MACs baked into the camera firmware image → score 55, alerts. Matched byte-for-byte, because the bare `00:03:7f` OUI is shared with every other Atheros device on earth; only an *unprovisioned* unit still transmits them.
- **addr1 receiver-side detection** (catches sleeping cameras)
- **addr3 BSSID fallback** for randomized addr2 frames (now ON by default)

### 2. Wildcard Probe Signature (DeFlockJoplin)
- Flock cameras send **probe requests with empty SSID**
- Combined score OUI+probe = 62 → HIGH CONFIDENCE on first match
- Field-tested: 11/12 cameras detected, only 2 false positives

### 3. SSID Pattern Matching — including LAA-MAC cameras (issue #43)
- Patterns: `"Flock Camera net."`, `"Flock-XXXXXX"`, `"FLOCK-XXXXXX"`, `"penguin"`, `"pigvision"`, `"fs ext battery"`
- `"Flock Camera net."` cameras use **locally-administered MACs** (OUI matching won't work)
- `ALERT_LAA_SSID` type detects these — SSID is the sole WiFi handle
- Sequential-MAC heuristic: `:DE`/`:DF` last-byte pair on adjacent channels → +10 pts

### 4. BLE Detection + Cross-Correlation (`ENABLE_BLE_SCAN=1`)
- Passive NimBLE scan for Flock BLE advertisements
- Checks: mfr-ID `0x09C8` (XUNTONG/Flock), Raven service UUIDs (GainSec) **plus the whole Raven `0x3100`–`0x3500` service range**, device names, the **Flock accessory GATT service** (`e8ccbb38-…`) and the **Nordic legacy DFU service** — plus name *shapes* a keyword list can't express: `Penguin-NNNNNNNNNN`, a bare 10-digit serial, `DfuTarg`
- Advertised device names are reported as `device_name` in the JSON/logs
- **BLE_COEX_MODE=1** (default for all `-ble` environments): ESP-IDF SW coexistence scheduler
  runs WiFi promiscuous + BLE simultaneously — no promiscuous pause needed
- BLE hit within 60 s of WiFi hit → +20 confidence bonus

### 5. Multi-Address Matching
- **addr2** (transmitter) — standard detection
- **addr1** (receiver) — catches cameras receiving probe responses
- **addr3** (BSSID) — fallback for randomized MACs

### Detection method reference

Every detection carries a `detection_method` string in the serial JSON (and in
the dashboard/CSV export). The full set:

| `detection_method` | Protocol | Fires when | Score |
|---|---|---|---|
| `oui_addr2` | `wifi` | `addr2` matches a high-confidence Flock OUI | 40 |
| `fw_default_mac` | `wifi` | `addr2` is an exact **factory-default** camera radio MAC (`00:03:7f:50:00:01` / `…:4f:00:16`) — an unprovisioned unit | 55 |
| `oui_addr1` / `oui_addr3` | `wifi` | OUI in the receiver (`addr1`) / BSSID (`addr3`) — AP-echo paths, deliberately quieter | 18 / 12 |
| `wildcard_probe` | `wifi` | High/mfr-tier OUI **+** empty-SSID probe request | 62 (`oui_addr2`+`wildcard_probe`) / 20 mfr |
| `ssid` | `wifi` | SSID keyword hit from a globally-administered MAC | 32, or 45 for exact `Flock Camera net.` |
| `laa_ssid` | `wifi` | SSID keyword hit from a **locally-administered** MAC (issue-#43 cameras) | +12 over `ssid` |
| `oui_mfr` | `wifi` | Contract-manufacturer OUI (Liteon/USI, **`00:03:7f`** Qualcomm Atheros) — silent alone | 20 |
| `soundthinking` | `wifi` | SoundThinking/ShotSpotter acoustic-sensor OUI | 35 |
| `ble_mfr_id` | `ble` | BLE manufacturer data company ID `0x09C8` (XUNTONG/Flock) | 45 |
| `ble_name` | `ble` | Device name keyword **or** shape match (`Penguin-NNNNNNNNNN`, bare 10-digit serial, `FS Ext Battery`, `DfuTarg`, …) | 35 |
| `ble_raven_uuid` | `ble` | Raven service UUID — the named table **or** anywhere in `0x3100`–`0x3500` (the range covers `0x3101`/`0x3102`, which leak GPS) | 45 |
| `ble_flock_gatt` | `ble` | Flock accessory service `e8ccbb38-…` or Nordic legacy DFU service | 45 |

Notes for dashboard consumers:
- BLE rows also carry **`device_name`** (the advertised name) — WiFi rows leave it empty.
- `protocol` is `wifi` / `ble`; `band` (`wifi_2_4ghz` / `wifi_5ghz`) is preserved separately.
- The Flask API additionally tags detections with **`matched_signatures`** and
  **`firmware_sig`** — `firmware_sig: true` means at least one signature from the
  camera-firmware image matched (see `datasets/firmware_derived_signatures.md`),
  as opposed to a community-research OUI hit.

**Confidence tiers:** < 30 = LOW (log only) · 30–59 = PROBABLE · ≥ 60 = HIGH (alert)

See [DETECTION_IMPROVEMENTS.md](DETECTION_IMPROVEMENTS.md) for full scoring tables and examples.

---

## 🧪 Native Unit Tests

The detection pattern library (`fy_detect.h`) is fully tested via a host-side
Unity test suite — no ESP32 hardware needed:

```bash
cd flock-you-esp32
pio test -e native                         # run all 62 tests
pio test -e native -f test_ble_matching    # MAC / BLE name / GATT / mfr-ID tests (41)
pio test -e native -f test_uuid_matching   # Raven UUID + range / parsing / fw version (21)
```

All **62 tests pass** against the current `fy_detect.h` / `fy_confidence.h`.  The
test suite covers:
- All 33 high-confidence Flock OUI prefixes (case-insensitive)
- All 7 contract-manufacturer OUIs (Liteon/USI + Qualcomm Atheros `00:03:7f`)
- SoundThinking OUI isolation (not in high or mfr lists)
- **Firmware-default radio MACs match on all six bytes** — near-misses in the
  same `00:03:7f` block (`…:50:00:02`) must *not* match (firmware-derived set)
- BLE device name substring matching (case-insensitive)
- BLE name **shape** matching: bare 10-digit serial, `Penguin-` + 10 digits,
  `FS Ext Battery`, `DfuTarg`, plus rejection of wrong digit counts / trailing junk
- BLE mfr-ID `0x09C8` match + rejection of the old incorrect `0x05A7`
- All 8 named Raven 128-bit GATT service UUIDs (case-insensitive)
- Raven service **range** `0x3100`–`0x3500`, including `0x3101`/`0x3102` (the
  GPS-leaking services the named table alone missed) and out-of-range rejection
- 16-bit service parsing from both UUID shapes (canonical 128-bit and `0x3101`)
- Flock accessory / Nordic DFU GATT UUIDs, and that the Flock accessory service
  is *not* reported as a Raven UUID
- Raven firmware version estimation from UUID categories

---

## 🎵 Audio Feedback

### Startup Sound
**Super Mario Bros. World 1-2** (underground theme)
- 6 notes: C5 → C4 → A4 → A3 → G#4 → G#3
- Confirms buzzer is working

### New Detection
**Two fast ascending beeps** (2000 Hz → 2800 Hz)
- First time seeing a camera MAC
- Or camera reappears after 30+ seconds
- This is the **only** runtime audio alert — the firmware does not emit
  any periodic/idle "still tracking" beep. Audio fires exclusively on a
  genuine new-detection event (`confidence >= CHIRP_MIN_CONFIDENCE`).

### Visual
**Onboard LED flashes** on every detection
- Works even without buzzer

---

## 📱 Flask Dashboard (GPS Wardriving)

### Features
- Real-time detection visualization
- GPS coordinate tagging (USB puck or browser)
- Export formats: JSON, CSV, KML (Google Earth)
- Multi-device support
- Historical tracking

### Quick Setup
```bash
cd firmware/api
pip install -r requirements.txt
python flockyou.py
```

Open `http://localhost:5000` and select your serial port.

---

## 📺 LILYGO T-Dongle C5 — Display & RGB LED

The `lilygo-t-dongle-c5` and `lilygo-t-dongle-c5-ble` environments target the
**LILYGO T-Dongle C5** — a USB-C dongle packing an ESP32-C5 (dual-band WiFi 6 + BT 5),
an ST7735S **80×160 colour TFT**, and a **WS2812B RGB LED**.

### What shows on the TFT

| State | Display | RGB LED |
|---|---|---|
| Startup | Splash screen "T-Dongle C5 ready" → "Scanning…" | Blue blink × 3, then green |
| Idle scanning | `Scanning…` · Channel & detection count | Dim green |
| Detection (conf < 30) | Detection type (large) · MAC tail · RSSI · Channel · Confidence% | Dim green |
| Detection (conf 30–59) | Same, dark-orange background | Amber |
| Detection (conf ≥ 60) | Same, dark-red background | Red |

### Pin reference

| Signal | GPIO |
|---|---|
| TFT SCLK | 5 |
| TFT MOSI | 6 |
| TFT CS | 4 |
| TFT DC | 2 |
| TFT RST | 3 |
| TFT Backlight | 1 |
| RGB LED (WS2812B) | 11 |
| BOOT button | 9 |

### Flash commands

```bash
# WiFi-only (no BLE)
pio run -e lilygo-t-dongle-c5 -t upload

# WiFi + BLE (NimBLE 2.x required for ESP32-C5 BLE support)
pio run -e lilygo-t-dongle-c5-ble -t upload
```

> **Note:** The T-Dongle C5 environments are marked experimental (`continue-on-error` in CI)
> because ESP32-C5 toolchain support is still maturing in espressif32@6.7.0.

---

## 🔬 Technical Specs

### Detection
- **Channels:** 1, 6, 11 (customizable) — hops every 100 ms (~300 ms full rotation)
- **Channel lock:** on a confident hit, holds that channel for 5 s of quiet before resuming the hop
- **RSSI threshold:** -95 dBm (configurable)
- **Range:** 50-100m typical, 300m with external antenna
- **Latency:** <10ms from RF frame to alert

### Hardware
- **MCU:** ESP32-WROOM-32 (dual-core 240 MHz)
- **RAM:** 520KB (uses ~62KB WiFi-only, ~72KB with BLE)
- **Flash:** 4MB (uses ~0.8MB WiFi-only, ~1.0MB with BLE)
- **Power:** ~180mA @ 3.3V (WiFi active)
- **Battery:** 6-8 hours on 3,000mAh 18650

### Storage
- **SPIFFS:** 1MB partition
- **Capacity:** 200 unique detections with full metadata
- **Persistence:** CRC32-validated, atomic writes
- **Recovery:** Survives power loss mid-save

---

## 📦 What Makes This Special?

### vs. Original Flock-You (XIAO ESP32-S3)
✅ **85% cheaper** ($6 vs $85 for OUI-SPY)  
✅ **Same detection** (identical WiFi chipset)  
✅ **More available** (ESP32 everywhere, XIAO only Seeed)  
✅ **Easier to prototype** (breadboard-friendly)  
✅ **Larger community** (ESP32 has huge support)  

### vs. Other Solutions
✅ **Passive detection** (no transmission, legal)  
✅ **Proven accuracy** (field-tested research)  
✅ **Open source** (modify freely)  
✅ **Portable** (pocket-sized)  
✅ **Expandable** (add GPS, batteries, external antenna)  

---

## 🚗 Use Cases

### Privacy Awareness
- Know when you're being surveilled
- Document camera locations
- Share data with DeFlock community
- Raise awareness in your area

### Security Research
- Test detection algorithms
- Map surveillance infrastructure
- Contribute to open research
- Develop counter-measures

### Wardriving
- GPS-tagged detection mapping
- Export to Google Earth (KML)
- Build community databases
- Identify high-surveillance zones

### Vehicle Integration
- Dashboard mount
- USB power from car
- Audio alerts while driving
- Optional battery for portability

---

## 📋 Complete BOM

### Electronics
| Part | Qty | Unit Price | Total |
|------|-----|------------|-------|
| ESP32 DevKit | 1 | $5-6 | $5-6 |
| KY-006 Passive Buzzer | 1 | $1-2 | $1-2 |
| 400-pt Breadboard | 1 | $2 | $2 |
| Male-Male Jumpers (3) | 1 | <$1 | <$1 |
| USB Micro Cable | 1 | $1 | $1 |
| **Subtotal** | | | **$9-11** |

---

## 🐛 Troubleshooting

### No startup sound?
- Check passive (not active) buzzer
- Verify GPIO 25 connection
- Try swapping buzzer polarity
- Disable in code: `#define USE_BUZZER 0`

### No detections?
- No cameras nearby (drive to known locations)
- Check serial output (should show channel hopping)
- Lower RSSI threshold: `#define RSSI_MIN -100`
- Verify WiFi promiscuous mode enabled

### Compilation errors?
- Update PlatformIO: `pio upgrade`
- Check board definition: `esp32dev`
- Verify partition file exists
- Clean build: `pio run -t clean`

**[Full Troubleshooting Guide](SOLDERLESS_BUILD_GUIDE.md#-troubleshooting)**

---

## 🤝 Contributing

### Ways to Contribute
- 📸 Share your build photos
- 🐛 Report bugs & issues
- 💡 Suggest features
- 📝 Improve documentation
- 🧪 Field-test and report accuracy
- 🗺️ Submit camera locations to DeFlock

### Remix Culture
This project is licensed **CC-BY-SA 4.0**:
- ✅ Use commercially
- ✅ Modify and remix
- ✅ Share freely
- 📝 Credit original authors
- 🔄 Share-alike license

---

## 🏆 Credits

### Original Firmware
- **colonelpanichacks** - Original Flock-You creator
- **ØяĐöØцяöЪöяцฐ (@NitekryDPaul)** - WiFi research, 30 OUIs, addr1 technique
- **Michael / DeFlockJoplin** - Wildcard-probe signature, 31st OUI
- **Will Greenberg** - BLE manufacturer ID detection
- **DeFlock / FoggedLens** - Crowdsourced ALPR data
- **GainSec** - Raven BLE service UUIDs

### This ESP32 Port
- Modified for standard ESP32 (4MB flash, UART)
- Solderless assembly guide
- Business analysis & documentation
- Community testing & feedback

---

## ⚖️ Legal & Disclaimer

### What This Device Does
- **Passively receives** publicly-broadcast WiFi frames
- **Does not transmit** any signals
- **Does not authenticate** to networks
- **Does not decrypt** any data
- **Educational/research** purposes

### Legality
- Passive WiFi reception is **legal in most jurisdictions**
- Equivalent to listening to public radio broadcasts
- No different from WiFi analyzers or network sniffers
- **Always comply with local laws**

### Use Responsibly
- Respect privacy and property rights
- Use for legitimate security research
- Contribute findings to public good (DeFlock)
- Don't use to enable illegal activity

**The authors assume no liability for misuse.**

---

## 🔗 Resources

### Community
- **Original Repo:** [colonelpanichacks/flock-you](https://github.com/colonelpanichacks/flock-you)
- **De-Flock:** [deflock.me](https://deflock.me) - Crowdsourced camera maps
- **Research:** `firmware/datasets/` - Full methodology

### Hardware
- **ESP32:** [espressif.com](https://www.espressif.com/en/products/socs/esp32)
- **PlatformIO:** [platformio.org](https://platformio.org/)

### Learn More
- **WiFi Sniffing:** [ESP32 Promiscuous Mode](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html)
- **Privacy Tech:** [EFF Surveillance Self-Defense](https://ssd.eff.org/)

---

## 📈 Project Stats

- **Hardware Cost:** $5-11 (vs $85 OUI-SPY)
- **Build Time:** 5-10 minutes
- **Detection Accuracy:** Same as premium hardware
- **Supported Boards:** Any ESP32 with 4MB+ flash
- **Community:** Growing!

---

## 🎉 Get Started!

**You're 2 steps away from detecting surveillance:**

1. **[Buy hardware](https://amazon.com/s?k=ESP32+DevKit)** → $5-11
2. **Flash the firmware** → 10 minutes (commands under
   [Quick Start](#2-flash-firmware) above)

**Questions?** Check the docs or open an issue!

**Ready?** [Start Building →](SOLDERLESS_BUILD_GUIDE.md)

---

*Built with love for privacy, security, and open knowledge.*  
*Detect. Document. DeFlock.*
