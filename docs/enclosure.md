# Enclosure — dimensions and the printed parts

Spec §11 said the repo "owes it dimensions rather than a model". It now owes
both: the dimensions are here, and the model is in `models/src/wedge.scad`.

Design reasoning is in
[`design/specs/2026-09-20-enclosure-wedge-design.md`](design/specs/2026-09-20-enclosure-wedge-design.md).
This file is the reference: the numbers, and where each came from.

## The display panel

2.8" 320×240 SPI module, ST7789 (sold as ILI9341 — see
[`hardware-notes.md`](hardware-notes.md)). From the manufacturer's drawing, in
the module's own portrait orientation.

| Feature | Size (mm) | Position |
|---|---|---|
| PCB | 50.00 ±0.20 × 81.70 ±0.20 × 1.60 | — |
| Glass (B/L) | 50.00 ±0.2 × 69.20 ±0.2 | full width, **6.25 from each end** |
| Active area | 43.20 × 57.60 | **12.05 from each end**, 3.40 from each side |
| Mounting holes | Ø3.2 | **75.90 × 44.80 pitch** — 2.90 from each end, 2.60 from each side |
| Main header | 14 pins, 2.54 pitch, 33.02 span | 8.70 from one long edge |
| SD header | 4 pins, 2.54 pitch, 7.62 span | opposite short end |

Depth, measured on the part:

| | mm from the glass face |
|---|---|
| Glass proud of the PCB | 3.6 |
| PCB back | 5.2 |
| SD socket back | 7.8 |
| Header pin tips | 12.6 (datasheet: 12.5 max) |
| **With a female DuPont housing** | **20.9** |

The SD socket is **full size**, 26.6 × ~25, on the back against a **long** edge,
standing 2.6 proud.

> **Unresolved.** The panel was also measured as having ~7.15 mm more bare PCB at
> the pin end than the other, which the centred reading above does not reproduce.
> Centred is taken because it is the only reading under which a Ø3.2 hole at 2.90
> from the end fits inside the 6.25 bare strip. `aa_off` in the model is the
> single constant to change if a print proves otherwise.

## The ESP32 DevKit

30-pin WROOM-32, USB-C. Measured on the part.

| | mm |
|---|---|
| PCB | 51.7 × 28.5 × 1.6 |
| Chip proud of the PCB | 3.2 |
| Header pins proud | 8.0 |
| BOOT / EN centres | 3.2 from the USB end, 6.35 from each long edge (mirrored) |
| USB-C protrusion past the PCB edge | 1.6 |

## Jumper housings — the dimension that shapes the case

A female DuPont housing is **14.0 long** and seats against the **pin tip**, not
the PCB, so it stands `pin + 8.3` above the board:

| | Pin | Housing top above the PCB |
|---|---|---|
| DevKit | 8.0 | **16.3** |
| Panel | 7.4 | **15.7** |

Against a 5.2 mm display module, the wiring is three times the depth of the thing
it drives — and it appears twice. Every dimension of the enclosure follows from
it. Shortening a pin lowers the stack one-for-one until the housing reaches the
PCB at 5.7 mm of pin; below that there is no further gain.

## The printed parts

| Part | Size (mm) | File |
|---|---|---|
| Body | 90 × 48 × 70 | `models/build/wedge_body.stl` |
| Cover | 84.4 × 65 × 2.5 (+ snap tabs) | `models/build/wedge_cover.stl` |

A raked wedge: vertical back, flat bottom, front face 15° from vertical, open at
the back with a snap-on cover.

| | mm |
|---|---|
| Wall | 2.5 |
| Front face thickness | 3.6 — equal to the glass's proud height, so the glass finishes flush |
| Panel pocket | 82.1 × 50.4, with a 2.5 × 3.0 rim |
| Aperture | 61.0 × 46.6 — the live image plus 1.7 all round |
| Bezel | 14.5 sides, 12.93 top and bottom (along the rake) |
| Panel location | four Ø2.9 pins on the 75.90 × 44.80 pitch |
| DevKit bay | board at `y 16.5…45.0`, underside 18.8 above the inner floor |
| USB-C opening | left flank, 14.0 × 9.0 |

The DevKit mounts **component-side-up with its pins pointing down** into a well,
so BOOT, EN and the chip all face the cover — lifting it puts a finger on them,
and there are no access holes anywhere in the case.

## Regenerating

```bash
openscad -o models/build/wedge_body.stl  -D 'part="body"'  models/src/wedge.scad
openscad -o models/build/wedge_cover.stl -D 'part="cover"' models/src/wedge.scad
pytest tests/test_models.py
```

`models/build/` is generated and gitignored. The tests assert the clearances the
design depends on — the DevKit bay against the SD socket, the aperture against
the active area, the panel's housings against the back — so editing one constant
in isolation cannot silently close a gap. They also assert each part renders as a
**single manifold solid**, which catches an internal feature that merely touches
the shell instead of fusing into it.

## Assembly order

1. Panel onto the four locating pins in the front face, glass through the aperture.
2. DevKit onto the bay ledges, component side up, USB-C through the left flank.
3. Nine jumpers between them, routed down the inside of the back.
4. Cover on last.

To reflash, lift the cover: `docs/hardware-notes.md` records that this board needs
**BOOT held, EN pressed and released, BOOT released** before `pio run -t upload`,
because auto-reset does not work on it. The 1 µF EN–GND capacitor noted there
would remove the need.
