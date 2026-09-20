# Firmware-Derived Flock Safety Signatures

**Provenance:** extracted from a Flock Safety ALPR camera firmware image
(Qualcomm MSM8953 + QCA9377 radio, Android 8.1, codename `hpnotiq`),
analyzed 2026-09-16. Upstream source of this research:
`colonelpanichacks/flock-you` (the parent of this fork).

Every signature below comes out of that firmware image — default MACs baked
into NVRAM/OTP blobs, strings and constants in decompiled services and config
files, and GATT service definitions. This is the **firmware-derived** half of
our detection set; the community field-research half (@NitekryDPaul,
DeFlockJoplin) remains in
[`NitekryDPaul_wifi_ouis.md`](NitekryDPaul_wifi_ouis.md). Both are active —
main.cpp matches the union, because the two sets have near-zero overlap and
cover different hardware generations.

| Where the signature lives | Constant / table |
|---|---|
| `fy_detect.h` | `fy_oui_mfr[]` (`00:03:7f`), `fy_exact_macs[]`, `fy_ble_names[]` (`DfuTarg`), `fyCheckBleNamePattern()`, `fy_ble_gatt_uuids[]`, `FY_RAVEN_SVC_MIN/MAX` |
| `fy_confidence.h` | `CS_FW_DEFAULT_MAC`, `CS_BLE_GATT_STANDALONE`, `matchExactFwMac()` + `exact_mac_bytes[]` |
| `api/flockyou.py` | `FIRMWARE_*` constants + `firmware_signature_matches()` / `tag_firmware_signatures()` |

---

## WiFi

### OUIs and exact MACs

| Value | Meaning | Internal source |
|---|---|---|
| `b4:1e:52` (OUI) | Flock Safety's own IEEE-registered MA-L assignment | Already present in our high-confidence table (dougborg/PR#39); the dump confirms it |
| `00:03:7f` (OUI) | Qualcomm Atheros — the camera's QCA9377 radio | Kept in the **low-confidence** (mfr) tier deliberately: it is a ubiquitous chipset prefix, so on its own it is not evidence of a camera |
| `00:03:7f:50:00:01` (full MAC) | Factory-default radio address | `bdwlan30.bin` / `fakeboar.bin` (WLAN NVRAM). Checked **byte-for-byte** and scored high (`CS_FW_DEFAULT_MAC=55`) — this fires only on a unit that has not yet been provisioned, because provisioning rewrites the MAC |
| `00:03:7f:4f:00:16` (full MAC) | Factory-default radio address | `otp30.bin` (OTP / factory partition) |

The radio emits **broadcast probe requests at roughly 125 ms intervals,
channel-hopping** (Qualcomm LOWI geolocation scanning), so these prefixes
appear in `addr2` even with no AP association.

### SSID patterns

| Pattern | Meaning | Internal source |
|---|---|---|
| `Flock-XXXXXX` | SoftAP broadcast by the camera | `WifiApService.java`: literal `"Flock-"` + last 6 chars of the WiFi MAC; WPA2 password `security`. Covered by our existing `flock` keyword. |
| `Flock` | Bare advertising SSID on provisioned units | Same service |
| `FS Ext Battery` | FS Ext Battery pack SoftAP | Added to `target_ssid_keywords[]` from this dump |

---

## BLE

The camera's battery packs and accessories advertise over BLE. Detecting one
means a Flock unit (or its battery) is physically nearby.

| Signature | Alert type / method | Internal source |
|---|---|---|
| Complete local name `Penguin-NNNNNNNNNN` (exactly 10 digits) | `ble_name` | Penguin external battery pack advertising data |
| Complete local name = a bare 10-digit serial | `ble_name` (shape-matched — there is no keyword to search for) | Same pack, alternate firmware naming |
| Complete local name `FS Ext Battery` | `ble_name` | "Flock Safety External Battery" label |
| Complete local name `DfuTarg` | `ble_name` | Nordic legacy DFU target: the pack advertises this while being firmware-updated (dump bundles `no.nordicsemi.android.dfu` + `heated_battery_fw.bin`) |
| Manufacturer data company ID `0x09C8` (XUNTONG), payload embeds serials such as `TN72023022000771` | `ble_mfr_id` | Battery-pack advertisement manufacturer data (matched before this dump landed too) |
| Flock accessory GATT service `e8ccbb38-9532-46a8-9fe5-1814df172e6f` | `ble_flock_gatt` | Flock accessory GATT definition in the firmware. Key characteristic `628913a6-8701-40ff-a3ce-8f453ff0818d`, control characteristic `bb18d1d2-fe71-439f-9529-d4b472d139b5` |
| Nordic legacy DFU service `00001530-1212-efde-1523-785feabcd123` | `ble_flock_gatt` | Same update path as `DfuTarg` |
| Raven camera GATT services, 16-bit range **`0x3100`–`0x3500`** | `ble_raven_uuid` | Exposed unauthenticated; `0x3101`/`0x3102` leak GPS latitude/longitude |

> **Why the Raven range check matters:** we previously matched only the named
> round-hundred services (`0x3100`, `0x3200`, …). The services that actually
> leak GPS are `0x3101`/`0x3102`, which are *not* in that list — so
> exact-string matching silently missed the highest-value services. Matching
> the whole range catches them (`fyService16FromUuidString()` on the ESP32, the
> equivalent regex in `api/flockyou.py`).

---

## Bluetooth Classic (host-side corroboration only)

The ESP32's NimBLE stack cannot do Classic BT, so these signals are documented
for host-side companion tooling rather than the firmware matcher. They are
generic Qualcomm/Android defaults and only meaningful *alongside* one of the
BLE signatures above.

| Signature | Internal source |
|---|---|
| Device name `msm8953_32` | Fallback Classic BT name from the MSM8953 platform base |
| Device name `Android` | Persist property `net.bt.name=Android` with no vendor override |
| SDP Device-ID: vendor `0x001D` (Qualcomm), product `0x1200` | `bt_did.conf` |

---

## Notes

- These signatures target the specific hardware generation in the dump
  (MSM8953 + QCA9377, Android 8.1). Other Flock hardware generations — e.g.
  Espressif-based units covered by the community OUI list — will not match all
  of them, which is exactly why both sets stay enabled.
- The Classic-BT names are low-specificity on their own; treat them as
  corroborating signals, not standalone detections.
- **Field coverage is not yet measured.** These paths are build- and
  unit-test-verified, and `beacon_test.cpp` scenarios 12–14 exercise them
  end-to-end against a second board — but no live camera has been observed by
  this repo since they were added. If field tests miss visually-confirmed
  cameras, the `FY_SNIFF_STATS` counters are the first thing to check
  (`stats gate … fwmac=…` for the default-MAC path, `stats ble … gatt=…` for
  the Flock accessory/DFU service) — see
  `.clinerules/04-detection-methods.md`.

