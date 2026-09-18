# AT Departure Board

An ESP32 + 2.8" TFT that shows when the next bus and train actually leave, using
Auckland Transport's realtime feed.

Each service gets a lane, and the vehicle's position along its lane **is** its
time to arrival — it enters at the left twenty minutes out and pulls into the
stop as the countdown reaches zero. The point is that you can read it from
across a room without resolving any digits.

Status: display layer complete and testable without hardware. Firmware next.

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

## Development

```bash
python -m pytest                # everything
python tools/regolden.py        # ONLY when a render change is intended
python tools/export_sprites.py  # regenerate src/sprites.h after editing sprites
```

Renders are compared byte-exact against `tests/golden/`. A failing golden test
means the render changed — if that was intended, regenerate and commit the
goldens in the same commit as the change.

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

Sprites are authored as role grids — `B` for body, `W` for window — rather than
literal colours, which is what lets one bus sprite render in whatever colour its
route is, and what lets a theme reinterpret every role at once. `src/sprites.h`
is generated from the same data and committed, so the firmware build never needs
Python.

Art is never typed by hand. Edit `tools/author_art.py`, which draws it
parametrically, then regenerate.

## Hardware

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

Drive the backlight from GPIO32 rather than 3V3 — dimming and quiet hours depend
on it being PWM-able.

## Documentation

- `docs/at-api-notes.md` — the AT API as it actually behaves, verified against
  live endpoints. **Read this before touching the network code**; it differs
  from AT's own documentation in five places, including one that would make the
  board re-resolve every stop every night.
- `docs/superpowers/specs/` — design.
- `docs/superpowers/plans/` — implementation plans.

## Licence

MIT — see `LICENSE`.
