# Hardware and firmware notes — measured, not estimated

Everything here was measured on the actual board on 2026-09-15 by a throwaway
spike (`spike/heap/`, removed once it had answered its question — the code is
in the git history, last present at 026bc09). Where the design doc guessed a
number, the measured one is recorded next to it.

## The board

| | |
|---|---|
| Chip | ESP32-D0WD-V3 rev 3, dual core @ 240 MHz |
| Flash | 4 MB |
| PSRAM | **none** |
| USB-serial | CH340 (`1A86:7523`) |
| Shipped firmware | Espressif ESP-AT 1.1.2 — overwritten on first flash |

### Uploading needs the BOOT button held

Auto-reset into the bootloader does not work on this board: esptool resets the
chip but GPIO0 is not pulled low, so it boots the app and esptool reports
`Wrong boot mode detected (0x13)`.

What works reliably (2026-09-19): put the chip into download mode *first*, then
flash without letting esptool reset it.

1. Hold **BOOT**, press and release **EN**, then release **BOOT**. The chip now
   sits in the bootloader; the screen does not change.
2. `pio run -e esp32 -t upload`. `platformio.ini` sets
   `board_upload.before_reset = no_reset` and `upload_speed = 115200`, so
   esptool neither tries the broken auto-reset nor drops the link.

`--after hard_reset` via RTS *does* work, so the new firmware starts on its own.

Two ways it fails: `pio run -t upload` gives up after ~11 s, before a human can
do the buttons; and at **460800** baud the CH340 link drops right after the
baud switch (`No more data to read from the serial port`). Releasing BOOT
before EN also misses download mode (`No serial data received`).

Permanent fix if it becomes tiresome: a 1 µF capacitor between EN and GND.

## Memory — the design's budget holds with ~2x headroom

Paper estimate was ~85 KB of ~300 KB: 40 KB TLS + 35 KB sprite + 8 KB parser.

| Measurement | Value |
|---|---|
| Free heap at boot | 351,448 |
| **Largest contiguous block at boot** | **114,676** |
| 35,840-byte lane sprite | allocated, every byte touched |
| Free with sprite held | 315,592 |
| Free after WiFi up | ~209,000 |
| **TLS handshake cost** | **~44,000** (est. 40,000) |
| Free with sprite + TLS + parse live | ~157,000 |
| Largest block at that moment | 94,196 |
| **Lowest heap across a full run** | **150,932** |
| Leak check | returns to boot value exactly after every request |

### The per-lane sprite was necessary, not merely tidy

Free heap is ~351 KB but the **largest single block is ~114 KB**, because the
ESP32 heap is split across regions. A full-screen 320x240x16bpp framebuffer
needs **153,600 bytes contiguous** and therefore *cannot* be allocated at all.

The design's 320x56 per-lane sprite (35,840 bytes) was the only approach that
was ever going to work. Do not "optimise" it into a full framebuffer later.

## The display is an ST7789, not an ILI9341

The module is the common red "2.8\" TFT 240xRGBx320 V1.1" SPI board with an SD
slot and a touch footprint (touch unpopulated). It is sold as ILI9341, and the
design doc assumed that. The unit on hand has an **ST7789** controller. Found
by a bring-up spike (`spike/display/`, also removed — see the git history),
2026-09-19.

Wiring is exactly as in design doc §2, and it is right: nothing had to change.

TFT_eSPI configuration that works (build flags, no `User_Setup.h` edit):

```
-DUSER_SETUP_LOADED=1
-DST7789_DRIVER=1
-DTFT_RGB_ORDER=TFT_BGR
-DTFT_INVERSION_OFF=1
-DTFT_WIDTH=240  -DTFT_HEIGHT=320
-DTFT_MISO=19 -DTFT_MOSI=23 -DTFT_SCLK=18 -DTFT_CS=15 -DTFT_DC=2 -DTFT_RST=4
-DSPI_FREQUENCY=27000000
```

`setRotation(1)` and `(3)` give a clean 320x240 landscape; all four rotations
fill the glass edge to edge. The backlight on GPIO32 is driven HIGH by the app.

How each wrong setting looks, so it can be recognised again:

| Setting | Symptom |
|---|---|
| `ILI9341_DRIVER` | Landscape drawn un-rotated into 240 columns; the other 80 are static. Other rotations cut up / off-centre. Red and blue swapped. |
| ST7789, default inversion (ON) | Whole image a photo negative: black background shows white. |
| ST7789, default colour order (RGB) | Red and blue swapped. |
| Both defaults together | Red→yellow, green→magenta, blue→cyan, yellow→red. Looks random; it's inversion plus swap. |

The panel **does not answer reads**: RDDID (`0xD3`) returns `00 00 00` and
RDDST returns constant garbage. So the controller can't be detected in software;
identify it by rendering, as above. Don't build anything that depends on
reading back from the panel.

## Display performance

Measured on 2026-09-19 over a 225 s capture covering every scene in both themes,
SPI at 27 MHz (the default set in `platformio.ini`):

| Measurement | Value |
|---|---|
| SPI clock | 27 MHz |
| **Worst draw time per frame** | **59 ms** |
| Frame rate (all scenes) | 14.9–15.1 fps |
| **Lowest heap (min-ever)** | **311,588 bytes** |
| Largest contiguous block | 110,580 bytes |
| Flash used | 25.3% (331,741 of 1,310,720 bytes) |
| RAM | 6.9% |

The frame budget is 66 ms per frame at 15 fps; 59 ms leaves 7 ms of headroom.

The band renderer redraws the board in 48-row horizontal bands, each 320×48×2
bytes. That is **30,720 bytes per band** against the design spec's estimate of
35,840 (320×56), with **5 pushes per frame** to refresh the screen. This keeps
memory flat — no full-screen framebuffer.

SPI 40 MHz was not tried: 15 fps is met at 27 MHz and 40 MHz remains as headroom
for when WiFi and TLS run alongside.

### Double-precision sin() is software on this chip

The first measurement on 2026-09-19 showed the Ghibli theme at 96 ms per frame
(10.3 fps) in the 1–2 lane scenes, while transit ran at 54–57 ms. The cause:
`hill()` and `shore()` call `sin()` on doubles, and the ESP32's FPU is
single-precision only, so those calls are emulated in software. The band
renderer redraws each lane once per band, which meant about 3,000 software
`sin()` calls per frame.

Precomputing them into lookup tables (`hill_at` and `shore_at` in
`lib/core/src/shapes.cpp`, filled from the same functions so the values cannot
diverge) brought Ghibli to 58–59 ms per frame and unlocked 15 fps across all
scenes. The lesson: keep `double` maths out of the per-frame path.

## Flash is the tighter constraint

| Build | Flash used |
|---|---|
| Bare Arduino + heap probe | 265 KB (20%) |
| \+ WiFi, TLS, HTTPClient, ArduinoJson | **917 KB (70%)** |

70% of the default 1.31 MB app partition is gone before TFT_eSPI, the web
portal, or the sprite data exist. **The firmware will need a custom partition
table** — `huge_app.csv` or a hand-rolled one, noting OTA is a non-goal so a
single app partition is fine.

## The bug that would have shipped: chunked transfer-encoding

AT's GTFS API responds with `Transfer-Encoding: chunked`. The bytes on the wire
begin:

```
2561\r\n{"data":[{"type":"stoptrip", ...
```

The standard ESP32 idiom — `deserializeJson(doc, http.getStream())` — reads
`2561`, parses it as a valid JSON **number**, stops, and returns
`DeserializationError::Ok`. The document is a number, `doc["data"]` is null, and
the board shows **zero departures with no error, forever**.

The silent failure happens specifically **when parsing with an ArduinoJson
filter**, which the firmware always does (to keep the document small). With
ArduinoJson 7.4.3, measured across all three combinations:

| Parse | Body | Result |
|---|---|---|
| filtered | chunked | `Ok`, 0 rows — **silent** |
| filtered | clean (de-chunked) | `Ok`, 1 row |
| unfiltered | chunked | `InvalidInput` — loud |

Filtering is what turns a loud failure into a silent one: without a filter the
chunk-size token derails the parse enough to be rejected outright; with a
filter it's accepted as a lone number and the empty result looks like success.

Measured on hardware, same URL, same filter, back to back:

```
A. without de-chunking:  deserializeJson SUCCESS,  0 departures   <-- silent
B. with de-chunking:     deserializeJson SUCCESS, 19 departures
```

19 matches what `curl` returns for that URL. The fix is a small Stream wrapper
that unwraps chunk framing while keeping memory flat — it now lives in
`lib/core/src/dechunk.{h,cpp}`. `test/test_dechunk` pins both the bug (filtered
+ chunked without the wrapper) and the cure (filtered + chunked through it), so
neither regresses silently again.

### But the two AT APIs differ — branch, never assume

| API | Framing | `http.getSize()` |
|---|---|---|
| `gtfs/v3/...` | `Transfer-Encoding: chunked` | `-1` |
| `realtime/legacy/tripupdates` | `Content-Length` | e.g. `2176` |

De-chunking a **non**-chunked body is equally broken: the wrapper reads
`{"status"...` as a hex chunk header, gets 0, and declares the stream finished,
producing `EmptyInput`. Observed exactly that when the wrapper was applied
unconditionally.

So the client must branch on `http.getSize() < 0` at runtime rather than on
which endpoint it thinks it is calling.

## The `tripid` filter really is load-bearing

`GET /realtime/legacy/tripupdates` with **no** `?tripid=` returned
**766,473 bytes** — the whole Auckland network. Streaming it with an ArduinoJson
filter still drove free heap from ~160,000 down to **51,800**, largest block to
42,996, and ended in `IncompleteInput`.

With `?tripid=` and four trips the same endpoint returns **2,176 bytes**.

The design already said the filter was "load-bearing, not an optimisation".
That is now measured: unfiltered, the board comes within ~50 KB of the floor and
fails messily rather than cleanly.

## NTP and DST work

`configTzTime("NZST-12NZDT,M9.5.0,M4.1.0/3", "pool.ntp.org")` gave
`2026-09-15 20:42 NZST isdst=0` — correct. NZDT begins 27 September 2026; the
rule is in place and should flip on its own. Worth re-checking on the 28th.

## End-to-end, verified on hardware

The whole data path ran on the board on 2026-09-15 at 22:05, in one pass, with
the sprite held throughout:

```
A. stoptrips, no de-chunking              ->  0 departures   (silent failure)
B. stoptrips, de-chunked                  -> 11 departures
     22:04 O-W-201 dir 0 / 22:06 O-W-201 dir 1 / 22:26 E-W-201 dir 0
C. realtime ?tripid=, content-length      ->  4 entities
     delays -6, +16, -52, +66 seconds
```

The runtime branch picked `chunked -> de-chunking` for GTFS and
`content-length -> direct` for realtime within the same run, which is the
behaviour the firmware needs. Lowest heap across the whole pass: **151,032**.

Those delays are live: a train 52 s early and another 66 s late. Schedule alone
would have been wrong by over a minute in both directions.

## Live data on the board

Measured on the board on 2026-09-19, SPI at 27 MHz, WiFi and TLS live against
the real AT API:

| Measurement | Value |
|---|---|
| Frame rate | 15.1–15.2 fps |
| Worst draw time per frame | 55–57 ms (against a 66 ms budget) |
| Lowest heap (min-ever) | 94,944 bytes |
| Largest contiguous block | 94,196 bytes |
| Steady-state free heap | ~157 KB |
| Fetch task stack (core 0, 16,384 B) | 12,200 B free high-water mark — 4,184 B used at peak (16,384 − 12,200); the task performs the TLS handshakes, so this peak includes them |
| Live build flash | 75.6% (~990 KB of 1.31 MB) |
| Live build RAM | 25.8% |
| Demo build flash | 26.7% |

SPI stayed at 27 MHz: 15 fps held with the network running, so the 40 MHz
headroom noted above was not needed.

The lowest heap seen anywhere in this plan was **149,444 bytes**, during a TLS
handshake in `setup()` (the earlier self-check) — lower than the 157 KB
steady-state figure above but well clear of the ~95 KB floor seen during a live
fetch.

### TLS: the pinned root, and why the root

The board pins **DigiCert Global Root G2**, SHA256
`CB:3C:CB:B7:60:31:E5:E0:13:8F:8D:D3:9A:23:F9:DE:47:FF:C3:5E:43:C1:14:4C:EA:27:D4:6A:5A:B1:CB:5F`,
valid to 2038. The root is pinned rather than the leaf because the leaf
certificate expires in March 2027 — pinning it would mean the board silently
stops trusting AT's API a year from now. The root is stable for over a decade.

### NZ time rules live outside the standard library

Windows' UCRT (used for `pio test -e native`) mis-handles the POSIX TZ string:
it placed 2026-09-28 12:00 NZDT at 11:00 UTC, an hour wrong. So the NZ
DST rules are hand-implemented in `lib/core/src/nztime.cpp` rather than left to
the C library's TZ parsing.

### Flash headroom

Flash is at 75.6% for the live build. The portal (spec §3, not yet built) will
likely need `huge_app.csv` rather than the default partition table.

### First boot needs a wait for WiFi association

The first fetch attempt on first boot returned HTTP `-1` for both lookups: it
ran before WiFi association had completed. It succeeded once the code was
changed to wait for association rather than just for `WiFi.begin()` to
return.

## Still to verify on hardware

- PWM dimming driven by the app (the LEDC path itself is verified).
- The WiFi-outage path: pull WiFi, expect `stale Nm` with the last good data
  kept, the lanes dimmed and the vehicles still animating, then recovery without a reboot when
  WiFi returns. This has **not** been performed on hardware. The code paths
  were reviewed but not observed running.
