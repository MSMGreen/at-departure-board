# Assembly — wiring and fitting the printed case

Physical build, start to finish. The electrical side is nine wires and no
components; most of the fiddle is mechanical.

Dimensions are in [`enclosure.md`](enclosure.md); why the case is shaped this way
is in [`design/specs/2026-09-20-enclosure-wedge-design.md`](design/specs/2026-09-20-enclosure-wedge-design.md).

## What you need

| | |
|---|---|
| Printed `wedge_body.stl` and `wedge_cover.stl` | from `models/build/` |
| 2.8" 320×240 SPI panel | ST7789, no touch |
| ESP32 WROOM-32 DevKit, 30-pin | USB-C on this build |
| **9 jumper wires** | Female at **both** ends, ~100 mm — see below if you only have M-F |
| USB-C cable and a 5 V supply | it is permanently powered |

**They must be female at both ends.** Both boards present male pins, and
male-to-female jumpers cannot be chained into a female-to-female one: join two
end to end and you get `F —[M·F]— M`, still female at one end and male at the
other. Adding a third does not change it. The only way to bridge two male
headers with M-F jumpers is to get rid of one set of male pins — see below.

**100 mm is the right length.** Measured against the model, the runs from a
DevKit pin to a panel pin are 24–65 mm straight-line, 31–84 mm once bends and
routing are allowed for. 100 mm covers the longest with slack; the excess on the
short runs coils into the void beside the DevKit.

### If you only have male-to-female

Both boards normally arrive with their headers already soldered, so the male pins
are a given. **Do not desolder the panel's 14-pin header to get around this** —
removing 14 through-plated pins without lifting a pad is far harder than anything
else in this build, and there is no need.

**Buy nine F-F jumpers.** A few dollars. No soldering anywhere, and §4 becomes the
whole job. If you can wait for the post, stop here.

**Or make nine F-F from eighteen M-F.** Cut each jumper roughly in half, keep the
female halves, and splice two of them together — solder the bare ends, slide
heatshrink over the joint. Nine splices and you have exactly what you need.

This is the easiest soldering in the whole project: wire to wire, in free air,
nothing to overheat and no pads to lift. It is considerably more forgiving than
fitting a header, and it touches neither board.

| | F-F bought | Spliced from M-F |
|---|---|---|
| Soldering | none | 9 wire-to-wire joints |
| Jumpers used | 9 | 18 |
| Risk to the boards | none | none |
| Length to aim for | 100 mm | cut each half to ~50 mm |

Runs are 24–65 mm straight-line and 31–84 mm routed either way, since both keep
the panel's existing header. Aim for about 100 mm end to end and coil the slack
beside the DevKit.

Whatever you do, do not rely on twisted-and-taped joints. This board is powered
continuously and lives behind a closed cover.

The printed parts do not change either way.

## 1. Soldering — probably none

Both boards usually ship with headers fitted. Check before you buy an iron.

**If the panel's 14-pin header is loose** (uncommon — most ship fitted), solder it to the **back** of
the panel, plastic spacer against the PCB, long pins pointing **away from the
glass**. Solder all 14 even though only 9 are used — the outer pins carry the
mechanical load when you push jumpers on.

Pin protrusion is not critical. The case is drawn around the 7.4 mm measured on
this build, but a standard 11.5 mm header still leaves 5.5 mm of clearance behind
the panel.

**If the DevKit's headers are loose**, solder them so the pins protrude from the
side **opposite** the chip. That is the normal way round, and it is what lets the
board sit component-side-up with its pins pointing down into the well.

Nothing else is soldered.

## 2. Fit the panel

The panel drops into the pocket in the raked front face, glass through the
aperture, and is held by **four snap tabs** on the long edges.

1. Check the pocket for stringing or elephant's foot and clean it up. It is
   82.1 × 50.4 against an 81.70 × 50.00 board, so there is only 0.2 mm a side.
2. Sit the panel on the four **Ø2.9 locating pins** — they pass into the panel's
   own Ø3.2 mounting holes and set its position. The pins are 1.4 mm tall against
   a 1.6 mm board, so nothing stands proud.
3. Press the panel straight down until the four tabs click over its back face.
   Each tab has a 45° lead-in, so it should need thumb pressure, not tools.

To remove it, push the tabs outward with a thin blade and lift one edge.

**It fits either way round.** The panel and pocket are both symmetric, and the
glass and image are concentric with the PCB, so the screen lands centred
whichever end the header is at. Match the firmware to it: `setRotation(1)` for
pins on the right, `setRotation(3)` for pins on the left — both give a clean
320×240 landscape, per [`hardware-notes.md`](hardware-notes.md).

> If the tabs print too tight or snap off, the fallback is four dabs of hot glue
> at the corners of the PCB, clear of the glass. The locating pins are doing the
> accurate work; the tabs only stop it falling back into the case.

## 3. Fit the DevKit

It sits in the deep lower rear, **component side up, pins pointing down** into
the housing well.

1. Feed the USB-C end toward the **left flank**, through the 14 × 9 opening.
2. Lower the board onto the two ledges at its short ends. The upstands either
   side locate it; the board underside sits 18.8 mm above the inner floor.

Component-side-up is deliberate: BOOT, EN and the chip all face the open back, so
lifting the cover puts a finger on them. There are no access holes anywhere in
the case, and you will need those buttons on every flash — see §5.

## 4. Wire it up

Nine connections. No resistors, no level shifting.

| Panel pin | ESP32 | Note |
|---|---|---|
| 1 VCC | 3V3 | |
| 2 GND | GND | |
| 3 CS | GPIO15 | |
| 4 RESET | GPIO4 | |
| 5 DC / RS | GPIO2 | |
| 6 SDI / MOSI | GPIO23 | |
| 7 SCK | GPIO18 | |
| 8 **LED** | **GPIO32** | **not 3V3** — see below |
| 9 SDO / MISO | GPIO19 | |

The panel's nine signals are **pins 1–9, a contiguous run** from the VCC end. Pins
10–14 are the touch controller and stay unconnected, as do the four SD pins at
the opposite end of the board.

**LED goes to GPIO32, not 3V3.** On GPIO32 the backlight is PWM-able, which is
what any dimming behaviour needs. Tied to 3V3 it is full brightness forever.

Working order that avoids fighting yourself:

1. Connect all nine at the **panel** first, while the back is wide open and you
   can see the silkscreen — pushing jumpers on, or soldering them in. Start at VCC
   and work along; they are adjacent, so a miscount is easy to spot.
   Ribbon-style jumpers come joined in a strip; keeping pins 1-9 as one uncut
   ribbon makes a miscount almost impossible.
2. Route each wire down the inside of the back, toward the DevKit.
3. Push them onto the **DevKit** last, checking each against the table.
4. Coil the slack into the space beside the DevKit, between the board and the
   right-hand wall. Keep it clear of the USB opening.

On most 30-pin boards the majority of these GPIOs land on one header row, with
GPIO32 on the other — so do the long row first and the single stray last. Read
your board's silkscreen rather than trusting a pinout diagram; they vary.

If you want different pins, they are build flags in `platformio.ini`
(`-DTFT_CS=15` and friends), not code.

## 5. Cover, and flashing

Fit the cover last. It drops into the ledge around the back opening and snaps in.
The vents are not decoration — the ESP32 runs warm and holds WiFi and TLS up
continuously.

**Take the cover off to flash.** `docs/hardware-notes.md` records that auto-reset
into the bootloader does not work on this board, so every upload needs:

1. Hold **BOOT**
2. Press and release **EN**
3. Release **BOOT**
4. `pio run -e esp32 -t upload`

Both buttons face the open back, so this is one hand and no disassembly beyond
the cover.

A 1 µF capacitor between EN and GND makes auto-reset work and removes the step
for good. If you are comfortable with that solder joint it is worth doing once.

## 6. First power-up

Power it over USB-C before committing to the cover. Expect the backlight on and
something on screen within a second or two; the first fetch waits for WiFi
association, which takes a few seconds more.

If the screen stays dark, check LED → GPIO32 first. If it lights but shows
nothing, check DC → GPIO2 and CS → GPIO15. If the colours look wrong or the image
is a photo negative, that is a driver setting rather than wiring —
[`hardware-notes.md`](hardware-notes.md) has a table of exactly which wrong
setting produces which symptom.
