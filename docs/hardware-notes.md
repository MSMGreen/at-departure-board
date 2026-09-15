# Hardware and firmware notes — measured, not estimated

Everything here was measured on the actual board on 2026-09-15 by the throwaway
spike in `spike/heap/`. Where the design doc guessed a number, the measured one
is recorded next to it.

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

Every upload therefore needs: hold **BOOT**, tap **EN**, keep holding BOOT until
`Writing at...` appears, then **release BOOT**. Releasing matters — holding it
through the post-flash reset leaves the chip sitting in the bootloader printing
`waiting for download`.

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

Measured on hardware, same URL, same filter, back to back:

```
A. without de-chunking:  deserializeJson SUCCESS,  0 departures   <-- silent
B. with de-chunking:     deserializeJson SUCCESS, 19 departures
```

19 matches what `curl` returns for that URL. The fix is a small Stream wrapper
that unwraps chunk framing while keeping memory flat — see
`spike/heap/src/stage3.cpp` for a working implementation.

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

## Still to verify on hardware

- Conditional de-chunking (`getSize() < 0`) — written and built, not yet flashed.
  Stage 3 proved the de-chunker itself; this is the three-line branch on top.
- TLS with certificate pinning. The spike used `setInsecure()`, which is fine for
  a measurement and **must not** become the shipped behaviour.
- Anything involving the display: not wired yet.
