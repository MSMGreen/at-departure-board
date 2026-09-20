# Build your own board

Start to finish: parts, case, wiring, flashing, and pointing the board at the
stops outside your own door. No prior ESP32 experience assumed — if you can copy
a file and run a command, you can do this.

Roughly an evening, most of which is the 3D print running in the background.

1. [Parts](#1-parts)
2. [Print the case](#2-print-the-case)
3. [Wire it up](#3-wire-it-up)
4. [Install the toolchain](#4-install-the-toolchain)
5. [Get an AT API key](#5-get-an-at-api-key)
6. [Fill in secrets.h](#6-fill-in-secretsh)
7. [Flash the board](#7-flash-the-board)
8. [First boot](#8-first-boot)
9. [Point it at your stops](#9-point-it-at-your-stops)
10. [Troubleshooting](#10-troubleshooting)

Want to see it before you buy anything? `python tools/simulate.py --all --show`
renders every board state on your PC. Have the hardware but no API key yet?
Skip to step 7 and flash `esp32_demo`.

## 1. Parts

| Part | Notes |
|---|---|
| **ESP32 DevKit (WROOM-32)** | Any standard dev board with a USB port. The unit here is an ESP32-D0WD-V3 — dual core, 4 MB flash, **no PSRAM needed**, CH340 USB-serial. An ESP32-S3 or C3 would need the pin numbers in `platformio.ini` revisited |
| **2.8" 320x240 SPI TFT** | [The panel used here](https://www.aliexpress.com/item/1005004557916570.html). The common red "2.8 TFT 240xRGBx320 V1.1" module with an SD slot and an unpopulated touch footprint. Touch is not used. Listings say ILI9341; the one received was an **ST7789**, and the firmware is configured for ST7789 — see [the display note](#the-panel-is-probably-an-st7789) below |
| **9 jumper wires** | Female-to-female if both sides have pins. Or solder direct, which is what fits a case best |
| **USB cable** | Data, not charge-only. This trips more people up than anything else on this list |
| **Printed case** (optional) | Designed for this build and in this repo — `models/build/wedge_body.stl` and `wedge_cover.stl`, ~60 g of filament. See the next section |

A USB phone charger runs it permanently; it draws a few hundred mA at most.

### The panel is probably an ST7789

These modules are sold as ILI9341 and frequently arrive with an ST7789
controller instead. The build in `platformio.ini` is configured for **ST7789**,
which is what the panel linked above turned out to be.

If your screen comes up blank, mirrored, or with inverted colours, that is the
first thing to check: swap `-DST7789_DRIVER=1` for `-DILI9341_DRIVER=1` in
`platformio.ini` and re-flash. [docs/hardware-notes.md](hardware-notes.md)
records how this was diagnosed.

## 2. Print the case

Two parts, both committed ready to slice — **you do not need OpenSCAD to print
them**:

```
models/build/wedge_body.stl
models/build/wedge_cover.stl
```

To change anything, edit the model and re-render:

```bash
openscad -o models/build/wedge_body.stl  -D 'part="body"'  models/src/wedge.scad
openscad -o models/build/wedge_cover.stl -D 'part="cover"' models/src/wedge.scad
```

A raked wedge, 90 x 48 x 70 mm, with a snap-on back cover. Print the body **on
its back face**: the raked front becomes a 15 deg overhang, so it needs no
support, and the visible face comes out clean.

Everything is a named constant at the top of `wedge.scad` — case size, rake,
aperture, board positions — so a different panel or DevKit is an edit and a
re-run, not a remodel. `pytest tests/test_models.py` re-checks the clearances the
design depends on and that each part is still a single manifold solid.

Dimensions for both boards are in [enclosure.md](enclosure.md), if you would
rather draw your own. The board is happy in any enclosure — the panel is readable
from across a room, so somewhere at eye level near the door is the point of the
thing.

## 3. Wire it up

Nine connections, no resistors, no level shifting.

| Display pin | ESP32 pin |
|---|---|
| VCC | 3V3 |
| GND | GND |
| CS | GPIO15 |
| RESET | GPIO4 |
| DC / RS | GPIO2 |
| SDI / MOSI | GPIO23 |
| SCK | GPIO18 |
| **LED** | **GPIO32** |
| SDO / MISO | GPIO19 |

The one that matters: **LED goes to GPIO32, not 3V3.** On GPIO32 the backlight
is PWM-able, which is what any dimming behaviour needs. Tied to 3V3 it is
full-brightness forever.

The SD card pins on the module are unused — leave them unconnected.

If you want different pins, they are build flags in `platformio.ini`
(`-DTFT_CS=15` and friends), not code.

**Fitting it all into the printed case** — which wires to push on first, what
length to buy, how the panel is retained, and what (if anything) needs soldering:
[assembly.md](assembly.md).

## 4. Install the toolchain

You need Python 3.9+ and [PlatformIO](https://platformio.org/install/cli). The
PlatformIO CLI downloads the ESP32 toolchain itself on first build.

```bash
python -m pip install -r requirements-dev.txt   # renderer + tools + tests
python -m pip install platformio                # or use the VS Code extension
```

Check it before you plug anything in:

```bash
python -m pytest        # the renderer tests: should be all green
pio test -e native      # the firmware logic tests, compiled for your PC
```

`pio test -e native` needs a host C++ compiler (`g++`, Xcode command line tools,
or MSVC). If you don't have one, skip it — it is not needed to build firmware
for the ESP32, only to run the logic tests on your own machine.

## 5. Get an AT API key

Free, and takes a few minutes.

1. Register at [dev-portal.at.govt.nz](https://dev-portal.at.govt.nz/).
2. Subscribe to **both** the GTFS product and the Realtime product — the same
   key must cover both. A key subscribed to only one gives you a board that
   shows scheduled times but never a live delay, or nothing at all.
3. Copy the primary key.

Keys are per-person and rate-limited; don't commit yours or share it.

## 6. Fill in secrets.h

```bash
cp src/secrets.example.h src/secrets.h
```

Then edit `src/secrets.h`:

```c
#define WIFI_SSID     "your-network"
#define WIFI_PASSWORD "your-password"
#define AT_API_KEY    "your-key"
```

`src/secrets.h` is gitignored and must stay that way — **never commit it.**
2.4 GHz WiFi only; the ESP32 has no 5 GHz radio, which is the usual reason a
board never connects in a house with a combined-band network name.

The demo build reads no credentials but still includes the header, so an
unedited copy is enough for `esp32_demo`.

## 7. Flash the board

**Auto-reset into the bootloader does not work on these boards.** You put the
chip into download mode by hand, first, every time:

> Hold **BOOT**. Press and release **EN**. Release **BOOT**.

The screen won't change — that's expected. Then, immediately:

```bash
pio run -e esp32 -t upload        # the live board
```

or, with no API key and no WiFi:

```bash
pio run -e esp32_demo -t upload   # every board state, played in real time
```

PlatformIO auto-detects the serial port. With more than one board plugged in,
name it: `--upload-port COM8` on Windows, `--upload-port /dev/ttyUSB0` on Linux,
`/dev/cu.usbserial-*` on macOS.

The upload runs at 115200 baud on purpose: at 460800 the CH340 link drops mid-
flash. The firmware starts on its own once the upload finishes.

If this becomes tiresome, a 1 µF capacitor between EN and GND restores normal
auto-reset.

## 8. First boot

```bash
pio device monitor      # 115200 baud
```

A healthy live boot looks like this:

```
wifi: connected, ip 192.168.1.42 rssi -54
time: 1789412345
portal: listening on :80
BOOT-OK live
resolve 8213 -> ...  toward 1060 -> ...  routes 1
```

**Write down that IP** — it's the setup page. The board also appears as a new
ESP32 client in your router's device list, and it's worth giving it a DHCP
reservation there so the address doesn't move.

The panel should light up within a couple of seconds and start drawing. Lanes
fill in as the first fetch completes.

## 9. Point it at your stops

Open `http://<board-ip>/` in a browser on the same network.

The page has the location name shown on the panel, the theme, and up to **four**
watches. Each watch is four fields:

| Field | What to put in it |
|---|---|
| **Label** | What this lane is called on the panel — `to Wynyard Quarter`, `to work`, `city trains`. Up to 24 characters; the panel font is ASCII, so no macrons |
| **Stop code** | The number printed on the bus stop pole or listed on AT's site, e.g. `8213`. Hit **Check** and the board asks AT and shows you the stop's real name — if that isn't the stop you meant, you have the wrong code |
| **Route** | A route number to pin, e.g. `20`. Leave empty for "anything that stops here" |
| **Toward stop** | The stop code of somewhere further along in the direction you travel |

Then **Save and restart**. The board writes the config to flash and reboots;
it's back in a couple of seconds.

### Choosing the two tricky fields

**Route** — pin it when your stop also serves things you'd never board. A stop
served by the `20` you take and the `22R` you don't will otherwise show you a
`22R` that's irrelevant. A stop where anything going that way is useful — most
train platforms — should leave it empty. Empty is also what carries a rail watch
through a line rename, which Auckland does periodically.

**Toward stop** — this is *not* a direction like "inbound". Pick any stop
further along the route in the direction you'd travel; the board works out the
direction from it at every refresh, which is why a renumbered direction can't
silently flip your lane. Find the code the same way you found your own stop:
look up the route, follow it a few stops the way you go, take that pole number.
[docs/at-api-notes.md](at-api-notes.md) explains why it works this way.

If a watch can't prove its direction, the lane shows **check config** and
deliberately shows no departures rather than possibly-wrong ones.

### Themes

`transit` is the clean default. `ghibli` is softer, with different scenery and
palette. Switching themes applies immediately — no reboot.

Preview both on your PC before choosing:

```bash
python tools/simulate.py --scene two_up --theme ghibli --show
```

### Changing WiFi later

WiFi credentials and the API key are compiled in, not in the setup page, so
changing networks means editing `src/secrets.h` and re-flashing (step 7). The
stops and theme survive a re-flash — they live in NVS, not in the firmware
image.

## 10. Troubleshooting

| Symptom | Cause |
|---|---|
| `Wrong boot mode detected (0x13)` | The BOOT/EN dance didn't take. Hold BOOT, tap EN, release BOOT, then run upload — in that order, without letting go early |
| `No serial data received` | Released BOOT before EN, or a charge-only USB cable |
| `No more data to read from the serial port` | Upload speed too high for a CH340 link. The repo already pins 115200; don't raise it |
| No port found at all | Missing USB-serial driver (CH340 or CP2102 depending on your board), or a charge-only cable |
| `wifi: FAILED status 1` repeatedly | Wrong SSID or password in `src/secrets.h`, or a 5 GHz-only network |
| Screen stays black, board otherwise fine | Backlight not on GPIO32, or wrong driver — try `-DILI9341_DRIVER=1` |
| Colours inverted or mirrored | Wrong driver for your panel, as above. `-DTFT_INVERSION_OFF=1` and `-DTFT_RGB_ORDER=TFT_BGR` are the other two knobs |
| Lanes empty, serial shows `HTTP 401` or `403` | API key wrong, or not subscribed to both GTFS **and** Realtime |
| Times show but never a live delay | Key isn't subscribed to the Realtime product |
| A lane says **check config** | That watch's direction couldn't be derived — check its Toward stop is really further along the route in the direction you travel |
| A lane says **none tonight** | Correct and working: nothing more is scheduled today |
| Times drift then say **stale** | The board lost the network or the clock. It retries on its own; the dimmed panel is it telling you the numbers are old |

Still stuck? Open an issue with the serial log from boot — it says a lot.

## Where things live

- [../README.md](../README.md) — what the board is and how the repo is laid out.
- [hardware-notes.md](hardware-notes.md) — measured heap, the flashing dance,
  the ST7789 discovery.
- [at-api-notes.md](at-api-notes.md) — how AT's API actually behaves. Read this
  before touching the network code.
- [../CONTRIBUTING.md](../CONTRIBUTING.md) — running the tests, the golden
  renders, what a good change looks like.
