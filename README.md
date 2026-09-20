# AT Departure Board

[![CI](https://github.com/MSMGreen/at-departure-board/actions/workflows/ci.yml/badge.svg)](https://github.com/MSMGreen/at-departure-board/actions/workflows/ci.yml)

An ESP32 + 2.8" TFT that shows when the next bus and train actually leave, using
Auckland Transport's realtime feed.

Each service gets a lane, and the vehicle's position along its lane **is** its
time to arrival — it enters at the left twenty minutes out and pulls into the
stop as the countdown reaches zero. The point is that you can read it from
across a room without resolving any digits.

Status: live data, and configurable from a browser. The board fetches real AT
departures and realtime delays over WiFi/TLS, draws the board from them at
15 fps, and serves a setup page on your LAN for choosing stops and themes.
`DEMO_MODE` (synthetic departures, no network) still builds and is useful for a
no-key bring-up.

![The board in two themes](docs/theme-preview.png)

## Try it without hardware

```bash
python -m pip install -r requirements-dev.txt
python tools/simulate.py --all --gif --show
```

Renders every board state to `tools/out/`, including the two that are easiest to
get wrong: 1am with nothing left tonight, and a cancelled service.

Two themes ship: `transit` and `ghibli`. Sprites come in two sizes — large art
at 1–2 watches, compact at 3–4, because a 40px vehicle does not fit a 55px lane.

```bash
python tools/simulate.py --scene two_up --theme ghibli
python tools/author_art.py               # regenerate the vehicle art
```

## Build one

Roughly an evening's work. **[docs/SETUP.md](docs/SETUP.md) is the full
walkthrough** — parts, wiring, flashing, and pointing it at your own stops. The
short version:

| Part | What to get |
|---|---|
| Board | Any standard **ESP32 DevKit (WROOM-32)**. The unit here is an ESP32-D0WD-V3: 4 MB flash, no PSRAM, CH340 USB-serial |
| Display | 2.8" 320x240 SPI panel, no touch needed — [the one used here](https://www.aliexpress.com/item/1005004557916570.html). Sold as ILI9341; the unit received was an **ST7789**, which the build flags already handle ([why](docs/hardware-notes.md)) |
| Case | Printed from `models/src/wedge.scad` in this repo — a raked wedge with a snap-on back. Dimensions for both boards in [docs/enclosure.md](docs/enclosure.md), assembly in [docs/assembly.md](docs/assembly.md) |
| Wiring | Nine jumper wires, or solder direct — table below |

You also need a free [AT developer API key](https://dev-portal.at.govt.nz/),
subscribed to **both** the GTFS and the Realtime products with the same key.

```bash
python -m pip install -r requirements-dev.txt
pio test -e native                       # firmware logic, on your PC (needs a C++ compiler)
cp src/secrets.example.h src/secrets.h   # then fill in WiFi + AT key
pio run -e esp32 -t upload               # the live board
pio run -e esp32_demo -t upload          # the demo: no WiFi, no API key
```

Before each upload: hold BOOT, tap EN, release BOOT. Auto-reset into the
bootloader does not work on these boards, and `platformio.ini` is set up
accordingly — see [docs/hardware-notes.md](docs/hardware-notes.md).

`platformio.ini` does not name a serial port, so PlatformIO auto-detects one. If
you have more than one board plugged in, pass it explicitly:

```bash
pio run -e esp32 -t upload --upload-port COM8      # /dev/ttyUSB0 on Linux
```

## Point it at your own stops

Once the board is on your WiFi, **open `http://<board-ip>/` in a browser** on the
same network. The setup page holds the location name, the theme, and up to four
watches. Save and restart writes them to NVS and reboots the board, which takes
a couple of seconds.

The board prints its IP to the serial monitor at boot (`pio device monitor`),
and it is also the new ESP32 client in your router's device list:

```
wifi: connected, ip 192.168.1.42 rssi -54
```

Each watch is four fields:

| Field | Meaning |
|---|---|
| Label | What the lane is called on the panel, e.g. `to Wynyard Quarter` (24 characters) |
| Stop code | The number on the pole, e.g. `8213`. **Check** confirms it against AT and shows you the stop's name |
| Route | A route to pin, e.g. `20`. Leave it empty for "any route that stops here" |
| Toward stop | The stop you are travelling *toward*, never a direction — direction is derived at every refresh ([why](docs/at-api-notes.md)) |

Pin a route when the stop also serves services you would never board: stop 8213
here is served by `22R`/`22N` as well, so that watch pins `20`. A stop served
only by routes you'd actually take can leave Route empty, as a rail watch does
— that is also what carries a rail watch through a line rename.

WiFi credentials and the API key are **not** in the setup page. They are
compiled in from `src/secrets.h`, which is gitignored, so changing networks
means a re-flash. `src/watch_config.h` holds the watches the board seeds NVS
with on its first boot, and is only consulted then.

### The setup page is unauthenticated

It is plain HTTP on port 80 with no password, so anyone on your LAN can change
the board's stops or reboot it. That is a deliberate trade for a device with no
keyboard on a home network — the page exposes no credentials and the board
stores nothing sensitive — but do not put this on an untrusted or guest
network, and do not forward a port to it.

## Development

```bash
python -m pytest                # the renderer and the tools
pio test -e native              # the firmware logic (needs a C++ compiler)
python tools/regolden.py        # ONLY when a render change is intended
python tools/export_sprites.py  # regenerate src/sprites.h after editing sprites
```

Renders are compared byte-exact against `tests/golden/`. A failing golden test
means the render changed — if that was intended, regenerate and commit the
goldens in the same commit as the change. Both suites run in CI on every push
and pull request. [CONTRIBUTING.md](CONTRIBUTING.md) has the rest.

## Layout

| Concern | File |
|---|---|
| Colour roles, AT `route_color` handling | `tools/board/palette.py` |
| Themes (palette, sprites, scenery) | `tools/board/themes/` |
| Vehicle art (parametric source) | `tools/author_art.py` |
| Pixel-art vehicles | `tools/board/sprites.py` |
| What the screen is showing | `tools/board/model.py` |
| Lane geometry, ETA → x position | `tools/board/layout.py` |
| Drawing | `tools/board/render.py` |
| The canonical board states | `tools/board/scenes.py` |
| Export sprites to C | `tools/export_sprites.py` |
| Firmware logic, no Arduino headers | `lib/core/src/` |
| Firmware wiring: network, portal, panel | `src/` |

Sprites are authored as role grids — `B` for body, `W` for window — rather than
literal colours, which is what lets one bus sprite render in whatever colour its
route is, and what lets a theme reinterpret every role at once. `src/sprites.h`
is generated from the same data and committed, so the firmware build never needs
Python.

Art is never typed by hand. Edit `tools/author_art.py`, which draws it
parametrically, then regenerate.

## Wiring

ESP32 dev board (WROOM-32) and a 2.8" 320x240 ST7789 SPI panel, no touch (sold as
ILI9341 — see `docs/hardware-notes.md`).

| Display | ESP32 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| CS | GPIO15 |
| RESET | GPIO4 |
| DC / RS | GPIO2 |
| SDI / MOSI | GPIO23 |
| SCK | GPIO18 |
| LED | GPIO32 |
| SDO / MISO | GPIO19 |

Drive the backlight from GPIO32 rather than 3V3: there it is PWM-able, which is
what lets the panel dim rather than only switch off.

## Documentation

- [docs/SETUP.md](docs/SETUP.md) — build one from parts, start to finish.
- [docs/at-api-notes.md](docs/at-api-notes.md) — the AT API as it actually
  behaves, verified against live endpoints. **Read this before touching the
  network code**; it differs from AT's own documentation in five places,
  including one that would make the board re-resolve every stop every night.
- [docs/hardware-notes.md](docs/hardware-notes.md) — measured heap, the flashing
  dance, and why the panel is an ST7789.
- [docs/enclosure.md](docs/enclosure.md) — every dimension of the case and the
  two boards, and where each number came from.
- [docs/assembly.md](docs/assembly.md) — wiring and fitting it all into the
  printed case.
- [docs/design/](docs/design/) — the specs and implementation plans each
  feature was built from, kept as the record of why the code is shaped the way
  it is.

## Data and attribution

Departure data comes from [Auckland Transport's developer
API](https://dev-portal.at.govt.nz/) and is used under AT's developer terms —
check those terms before redistributing anything you build on it. The JSON under
`test/fixtures/` is captured AT API output, trimmed and used as test input.

This project is not affiliated with, endorsed by, or supported by Auckland
Transport.

## Licence

MIT — see `LICENSE`.
