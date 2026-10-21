# Experimental Bluetooth Photo v2

This is a local development implementation, disabled in normal release builds.
It has host and build validation; BLE timing, power use and physical panel
output still require hardware acceptance. It does not require server pairing.

## User flow and lifetime

1. Hold Refresh for 3–19 seconds and release to open Maintenance. Authenticate
   through the existing QR or encrypted, MITM-authenticated passkey channel.
2. Select Bluetooth mode in Companion. Firmware atomically saves manual mode
   and a 32-byte photo key, then returns `screen_mode`. The phone saves this key.
3. Leave Maintenance. Disconnect, physical exit or its absolute five-minute
   deadline reboots into the saved mode, giving the phone time to store the ACK.
4. In Bluetooth mode, a short button wake opens a photo session. It advertises
   for at most 90 seconds before authenticated commands, five minutes of idle
   time after authentication, and ten minutes absolutely. Opening a session
   does not paint another QR or connect Wi-Fi.
5. Send a photo. Once its bytes and SHA-256 are verified, firmware stops BLE,
   paints with the existing full-frame driver, clears the server ETag and sleeps.
   An immediate phone disconnect after commit does not discard that frame.
6. Hold Refresh again to enter Maintenance and select Automatic/Wi-Fi. Firmware
   revokes the photo key and reboots when Maintenance closes. Normal configured
   transport resumes and fetches a fresh frame.

A device without server registration can authorize Bluetooth mode by holding
Refresh through power-on/RESET for 3–19 seconds and releasing into Maintenance.
The existing captive-portal awake-window gesture still opens new-device setup;
it does not offer the manual-mode picker. No server pairing is needed in the
Maintenance authorization or photo path.

Bluetooth mode bypasses all automatic REST/MQTT/relay/deck updates, always-on
operation and the USB development loop. Normal manual sleep has neither BLE
advertising nor Wi-Fi, and wakes daily for a battery check or on a physical
button. Existing low-battery goodbye/recovery uses its hourly recheck; a photo
begin is refused for a known battery below 3450 mV. Unknown/mains readings retain
existing behavior. Holding Refresh for 20 seconds retains factory reset.

The mode and authorization survive reboot. A received frame is held in RAM
until painting, not saved as a photo file: boot, Maintenance and battery
messages can replace what is physically visible. Cold boot in manual mode
shows the instruction to press Refresh. Maintenance exit without changing mode
keeps manual mode; it cannot silently resume server updates.

Mode storage is one versioned NVS blob (`ble_photo/config`): version byte 1,
mode byte 0/1, then 32 key bytes. Absent configuration means Automatic. Repeated
Bluetooth authorization returns the same active key. Selecting Wi-Fi commits
mode 0 with a zeroed key; the next authorization generates a new key. Clear
Wi-Fi preserves this namespace and key. Factory reset erases them. An unreadable
or corrupt blob fails closed, requiring Maintenance/factory-reset recovery.
Changing mode does not erase stored server or network credentials.

## Discovery and authentication

Maintenance keeps service `7a5e0001-7b6d-4f8b-9c2e-1d0a5a110001` and existing
setup authentication. In supported experimental builds its public info adds
`"photo":2,"family":"tesserae","kind":"<stable catalog kind>"` and diagnostics
adds `screen_mode`. Fresh-device setup does not offer the photo feature.

Photo sessions use service `7a5e0008-7b6d-4f8b-9c2e-1d0a5a110001`, protocol 2,
advertisement mode/flags byte `0x04` and the board's existing raw hardware code.
They expose the same public-info 0002, QR-control 0003, events 0005 and a new
binary-data 0006 characteristic. All UUID suffixes match the service suffix.
The 0004 native-passkey characteristic rejects photo-mode writes. Photo
authentication uses only the persisted photo key with the existing BLE v2
HKDF/AES-256-GCM envelope, fresh session ID and per-connection nonce. A photo key
is never emitted as a setup QR. Native Maintenance writes additionally check
that the connection is encrypted and authenticated.

Control and binary traffic share directional AEAD counters, while each has its
own chunk reassembler. Every queued command and receiver buffer belongs to a
connection generation. Disconnect cancels incomplete data; reconnect requires
a fresh begin. Work queued by a previous connection cannot write a later one.

Authenticated Maintenance operation:

```json
{"op":"set_screen_mode","value":"bluetooth","request":123}
{"event":"screen_mode","value":"bluetooth","request":123,"photo_key":"<32 bytes base64url>"}
```

`value:"wifi"` revokes the key; its reply omits `photo_key`. `request` must be a
nonzero UInt32. Success replies are sent only after NVS commit. Mode changes do
not reboot while the phone is still storing that reply.

After a successful mode commit, the remainder of that Maintenance session only
accepts `diagnostics`, `set_screen_mode` (retry or another mode choice) and
`reboot`. Other operations, including Wi-Fi configuration, clear-Wi-Fi and
factory reset, return an error asking the phone to close Maintenance first.
Re-enter Maintenance for those actions. This prevents a disconnect-triggered
mode reboot from replacing another action after its acceptance reply.

## Capabilities and pixel layout

In the photo session, `{"op":"photo_capabilities"}` returns the full descriptor.
The following are the initial enabled targets, taken from the active driver's
buffer geometry, not inferred from the phone's hardware catalog:

| Experimental env | Kind | Buffer | Format | Stride | Bytes |
|---|---|---|---|---:|---:|
| `seeed-reterminal-e1004-ble-photo` | `seeed_reterminal_e1004` | 1200 × 1600 | `spectra6-msb` | 600 | 960000 |
| `seeed-reterminal-e1001-ble-photo` | `seeed_reterminal_e1001` | 800 × 480 | `mono1-msb` | 100 | 48000 |
| `seeed-reterminal-sticky-ble-photo` | `seeed_reterminal_sticky` | 480 × 800 | `gray2-msb` | 120 | 96000 |

Example exact E1004 descriptor (minified size is under the 512-byte control
message limit):

```json
{"event":"photo_capabilities","schema":2,"kind":"seeed_reterminal_e1004","width":1200,"height":1600,"visible_width":1200,"visible_height":1600,"x_offset":0,"format":"spectra6-msb","stride":600,"bytes":960000,"chunk_bytes":384,"digest":"e4a1365bc0232bd2666dbd403055fa710ab6cbe60cf2eaa2805140a7ea5f0f3f","row_order":"top_down","lifecycle":"manual"}
```

Rows are top to bottom. The left pixel occupies the most significant bit(s) or
nibble; row stride is exactly `ceil(width*bpp/8)`. The visible rectangle is the
entire buffer on these three boards, with `x_offset=0`. Maximum frame is 4 MiB.
The format IDs reserved by the shared contract are:

| Format | Bits/pixel | Codes |
|---|---:|---|
| `mono1-msb` | 1 | 0 black, 1 white |
| `gray2-msb` | 2 | 0 black through 3 white |
| `gray4-msb` | 4 | 0 black through 15 white |
| `bwry2-msb` | 2 | 0 black, 1 white, 2 yellow, 3 red |
| `spectra6-msb` | 4 | 0 black, 1 white, 2 yellow, 3 red, 5 blue, 6 green |
| `acep7-msb` | 4 | 0 black, 1 white, 2 green, 3 blue, 4 red, 5 yellow, 6 orange |

Only the three profiled formats are enabled by this firmware iteration. Other
hardware must supply verified driver geometry/color codes and pass physical
tests before the feature gate is widened.

`digest` is lowercase SHA-256 of the following canonical UTF-8 sequence with
decimal integers, pipe separators, no escaping, trailing pipe or newline:

```text
2|kind|width|height|visible_width|visible_height|x_offset|format|stride|bytes|384|top_down|manual
```

These placeholder names mean the descriptor's actual values. The phone checks
the descriptor and echoes its digest; it does not reconstruct it from a catalog.

## Transfer

The first implementation is stop-and-wait, one logical packet outstanding:

```json
{"op":"photo_begin","id":123,"schema":2,"bytes":960000,"format":"spectra6-msb","capability_digest":"<descriptor digest>","sha256":"<whole frame SHA-256>"}
{"event":"photo_ack","id":123,"offset":0}
```

`id` must be a nonzero UInt32. Schema, byte count, format and descriptor digest
must match current capabilities. Send logical binary packets to characteristic
0006 as `[id:UInt32 big endian][offset:UInt32 big endian][1..384 pixel bytes]`,
inside the existing BLE v2 encrypted/chunked framing. Each receives
`photo_ack` with the next expected offset. An exact duplicate may be ACKed
without appending; conflicting retries, future offsets, wrong IDs and out of
bounds packets are rejected.

```json
{"op":"photo_end","id":123}
{"event":"photo_received","id":123}
```

End succeeds only after the exact size and frame SHA-256 match. `photo_received`
attests verified receipt, not physical refresh success: the panel driver returns
void. There is no partial-refresh promise. A verified frame is immutable until
painted or the session is scrubbed after painting.

`{"op":"photo_cancel","id":123}` returns `photo_cancelled`, discards the
matching incomplete buffer and does not paint. Short frames, failed hashes,
cancelled and disconnected transfers cannot paint. There is no cross-connection
resume in this version.

## Reproducible validation

From the repository root:

```sh
sh tools/test_ble_photo.sh
sh tools/test_ble_setup_protocol.sh
python3 tools/generate_ble_photo_fixtures.py /tmp/photo-v2-fixtures
pio run -e seeed-reterminal-e1004-ble-photo -e seeed-reterminal-e1001-ble-photo -e seeed-reterminal-sticky-ble-photo
pio run -e seeed-reterminal-e1004 -e waveshare-133e6
```

Host scripts use the installed ESP-IDF mbedTLS sources under
`${PLATFORMIO_CORE_DIR:-$HOME/.platformio}`. Receiver/mode tests run with Address
and Undefined Behavior Sanitizers. They cover retransmissions, wrong offsets and
IDs, cancellation, bad hashes, short/oversized frames, immutable commit,
generation isolation, a complete 4 MiB frame, NVS commit failure, key reuse,
revocation, corrupt configuration and the post-mode-commit command gate.
Existing maintenance gesture and BLE
crypto vectors remain covered.

Small descriptor/hash goldens are in `test/fixtures/ble_photo`; the generator
recreates raw bars without storing large binaries in Git. E1004 bars are black,
white, yellow, red, blue and green from left to right, repeated on every row.
E1001 is black/white halves; Sticky is four increasing gray levels. To validate
an independently encoded phone frame through the actual C receiver and real
mbedTLS SHA implementation, run:

```sh
sh tools/test_ble_photo.sh /absolute/path/frame.bin <64-character SHA-256>
```

To keep a standalone receiver executable for cross-language tests (run from the
repository root):

```sh
photo_mbedtls="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-espidf/components/mbedtls/mbedtls"
cc -std=c11 -Wall -Wextra -Werror \
  -DMBEDTLS_CONFIG_FILE='"relay_mbedtls_config.h"' \
  -I src -I test -I "$photo_mbedtls/include" \
  test/test_ble_photo_fixture.c src/ble_photo.c \
  "$photo_mbedtls/library/sha256.c" \
  "$photo_mbedtls/library/platform_util.c" \
  "$photo_mbedtls/library/constant_time.c" \
  -o /tmp/tesserae-ble-photo-receive-fixture
python3 tools/generate_ble_photo_fixtures.py /tmp/photo-v2-fixtures
/tmp/tesserae-ble-photo-receive-fixture /tmp/photo-v2-fixtures/e1004.bin "$(cat /tmp/photo-v2-fixtures/e1004.sha256)"
```

An independent Swift E1004 encoder run has produced 960000 bytes, matched a
Python per-byte color oracle, and passed this C receiver in 384-byte chunks.
That cross-language frame SHA-256 was
`7e88b3db01f7e8c6b66ad4111ca47e48c891a5ff785e07fe33d3566455af2404`.
This validates byte layout, not BLE throughput or physical color output.

Local validation on 2026-10-21 used baseline
`421d51a41a843e94757b2261aed619aad0996b66` (1.52.0), ESP-IDF 5.5.2 and
pioarduino 55.03.37. All five envs in the commands above built successfully:
three opt-in experimental targets, normal E1004, and BLE-disabled
`waveshare-133e6`. Both host scripts and all three generated frame fixtures
passed. The build host needed Python 3.12 and PlatformIO 6.1.19; its older
Python 3.9 installation could not run this platform. A task-local PlatformIO
core reused the installed package cache without replacing the user's Python.

| Successful env | Static RAM bytes | Flash bytes |
|---|---:|---:|
| `seeed-reterminal-e1004-ble-photo` | 92724 | 1639109 |
| `seeed-reterminal-e1001-ble-photo` | 92740 | 1641257 |
| `seeed-reterminal-sticky-ble-photo` | 182916 | 2229633 |
| `seeed-reterminal-e1004` (normal BLE) | 91508 | 1632873 |
| `waveshare-133e6` (BLE off) | 85892 | 1390393 |

These linker totals exclude dynamic photo buffers and do not establish runtime
heap headroom. Local build log: `/tmp/tesserae-ble-pending-mode-builds.log`. Each
binary is at `.pio/build/<env>/firmware.bin`; build and hardware results are
separate acceptance steps.

## Hardware acceptance still required

- Run the complete Maintenance → Bluetooth authorization → disconnect/reboot →
  short-button photo flow on all three boards, using QR and passkey for
  Maintenance. Confirm a cold photo session needs neither Wi-Fi nor server.
- Paint color/gray/mono bars and an asymmetric orientation/corner test. Confirm
  geometry, row direction, color codes, black/white polarity and no hidden-column
  shift. Measure full refresh time and inspect the glass after the final ACK.
- Interrupt each transfer stage, reconnect, send a different image, inject bad
  hashes/conflicting retries, cancel and let deadlines expire. Confirm only a
  verified end can change the panel, including immediate final-ACK disconnect.
- Verify manual mode across timer wakes and power cycles with the configured
  server changing frames. Confirm Wi-Fi/BLE are off during sleep, verify button
  maintenance/reset gestures, then switch to Automatic and observe a fresh
  server frame. After saving a mode, request clear-Wi-Fi/factory-reset before
  closing: both must return an error rather than an acceptance reply. Re-enter
  Maintenance and confirm those actions work and retain/revoke the photo key
  as specified.
- Test low battery, charging/recovery, practical 960 KB throughput, MTU/packet
  fragmentation, RSSI/retry behavior, panel refresh current and asleep current.

Normal release envs do not enable this feature and must continue their existing
setup/maintenance and automatic transport behavior. There is no flashing,
hardware validation, OTA publication or release approval implied by these tests.
