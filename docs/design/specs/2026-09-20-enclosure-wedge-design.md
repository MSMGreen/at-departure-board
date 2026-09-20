# Enclosure: a raked wedge, modelled from scratch

Date: 2026-09-20
Status: designed, not built
Relates to: spec §11 (Enclosure), `docs/hardware-notes.md`
Supersedes: `2026-09-20-enclosure-rework-design.md`

## 1. What this covers

A new enclosure, modelled in this repo rather than adapted from a download. It
replaces the plan to rework the third-party ESP32-C3 case, which is kept as a
record because its §3 is the measurement that justifies this one.

| In | Out |
|---|---|
| A raked wedge body holding the panel and the DevKit | Any further use of `Body.stl`, `Lid.stl`, `Platform.stl` |
| A snap-on back cover | Mesh surgery, `trimesh`, `manifold3d` |
| Parametric OpenSCAD source in the repo | Wall mounting |
| `docs/enclosure.md`, the dimensions spec §11 has always owed | OTA, so no flashing-free future to design for |

## 2. The problem the form has to solve

One thing dominates every decision here: the jumper housings. They stand **16.3
mm above the DevKit** (§2.1) and **15.7 above the panel** (§2.3), against a
display module 5.2 mm thick. The wiring is three times the depth of the thing it
drives, and it appears **twice**.

The panel's header sits on a **short** edge. Rotated to landscape that edge is
vertical, so its strip runs the panel's full height — including the top, where a
wedge is thinnest. **That, not the DevKit, sets the case depth.** Measured, the
panel needs 20.9 mm behind its glass (§2.3); raked 15° with the panel top at
z = 59.3, that puts the housings at y = 36.1 against a back wall at 45.5 —
**9.4 mm clear** in a 48 mm deep case. The depth is set by the DevKit bay rather
than by the panel, though: see §5.

Once that depth exists, the DevKit fits in the deep lower rear at no extra cost,
which is what makes a wedge the efficient form.

### 2.1 Measured inputs — the DevKit

Measured by the owner on 2026-09-20. Carried over unchanged from the superseded
spec.

| Dimension | Value (mm) |
|---|---|
| PCB length | 51.7 |
| PCB width | 28.5 |
| PCB thickness | 1.6 |
| Envelope, component side (PCB + chip) | 4.8, so the chip stands **3.2** proud |
| Envelope including pins | 12.8, so the pins protrude **8.0** |
| Connector | **USB-C**, protruding 1.6 past the PCB edge |
| BOOT / EN centres | 3.2 from the USB-end edge, 6.35 from the long edge, mirrored |
| Female DuPont housing | 14.0 long, protruding **8.3 beyond the pin tip** when seated |

Housing top above the PCB is therefore **16.3** (8.0 + 8.3), and the housing
floats 2.3 above the board rather than resting on it.

### 2.2 The panel, from its datasheet

The manufacturer's drawing supersedes everything earlier drafts inferred from the
third-party case. Dimensions are the module's own, in its portrait orientation.

| Feature | Size (mm) | Position |
|---|---|---|
| PCB | 50.00 ±0.20 × **81.70** ±0.20 × 1.60 | — |
| B/L (glass) | 50.00 ±0.2 × 69.20 ±0.2 | full width; **2.60** from the SD end, **9.90** from the pin end |
| LCD/AA (active area) | 43.20 × **57.60** | 3.40 from each side; **6.25** from the SD end |
| Mounting holes | **Ø3.2**, four corners | ~2.7 from each edge |
| Main header | 14 pins, 2.54 pitch, 33.02 span | 8.70 from one edge |
| SD header | 4 pins (15–18), 2.54 pitch, 7.62 span | opposite end |
| Overall thickness | **12.5 max**, glass face to pin tips | — |

**The PCB is 81.70 long, not ~86.** An earlier draft read it off the third-party
case's 86.6 cavity and was wrong by 4.9 mm. That cavity is simply loose: the
panel is 81.70, so it can sit anywhere in a 4.9 mm range, which also means the
borrowed case is **not a reliable datum** and nothing in this design registers to
it any more.

**Everything is centred along the long axis**, and two independent chains in the
drawing close on it exactly:

| | From each end | Span |
|---|---|---|
| Glass (B/L) | 6.25 | 69.20 |
| Mounting-hole pitch | 2.90 | 75.90 |
| Active area | 12.05 | 57.60 |

`6.25 × 2 + 69.20 = 81.70` and `2.90 × 2 + 75.90 = 81.70`. The second is the one
that settles it: **75.90 is the hole pitch**, so the holes sit 2.90 from each end
and 2.60 from each long edge, which is exactly the ~2.7 measured on the part.

That also removes a contradiction an earlier draft raised. Reading the 2.60 as the
*glass* edge left no room for a Ø3.2 hole at that end. Read correctly — 2.60 is
the hole's offset from the long edge — a Ø3.2 hole at 2.90 from the end spans
1.30 … 4.50, comfortably inside the 6.25 bare strip, **at both ends**.

> One measurement still disagrees: the pin end was reported as carrying ~7.15 mm
> more bare PCB than the other. Nothing in the drawing reproduces that, and the
> centred reading is the only one under which the mounting holes physically fit.
> So the model takes centred and exposes `aa_off` as a single constant — if a
> print shows the image sitting toward the SD end, set it to 5.80 and re-render.

In landscape the PCB is **81.70 wide × 50.00 tall**, the glass 69.20 × 50.00, and
the active area 57.60 × 43.20, all sharing one centre.

### 2.3 Measured inputs — behind the panel

Measured by the owner on 2026-09-20.

| Measurement | Value (mm) |
|---|---|
| Glass proud of the PCB | 3.6 |
| Glass face to the header pin ends | 12.6 |
| Glass face to the back of the SD socket | 7.8 |
| SD socket | 26.6 along the PCB's **long** axis (31.0 from one end, 24.1 from the other), ~25 across from a **long edge** — not at a short end |
| Mounting holes | four corners, centres 2.7 from each edge |

Two of those cross-check each other. Taking the panel PCB as a standard 1.6:

- `3.6 + 1.6 + 7.4 = 12.6` — a 7.4 header pin
- `3.6 + 1.6 + 2.6 = 7.8` — a 2.6 SD socket

Both land on ordinary component heights from the same assumed thickness, which
is good evidence the panel PCB really is 1.6 and that the decomposition is right.

The SD socket is **full-size**, not microSD as an earlier draft assumed. Its
width was briefly recorded as 30.9, from `86.0 − 31 − 24.1` using the wrong PCB
length; against the datasheet's 81.70 it is `81.70 − 31 − 24.1 = 26.6`, which
matches the drawing's **27.00** to within a measurement's worth. The two
independent sources agreeing is also a check on the 81.70.

It matters less than feared either way: at 2.6 mm it is the shallowest thing back
there.

Depth stack, from the glass face:

| | mm |
|---|---|
| Glass | 0 → 3.6 |
| PCB | 3.6 → 5.2 |
| SD socket | 5.2 → **7.8** |
| Header pins (7.4) | 5.2 → 12.6 |
| Jumper housing (+8.3 beyond the pin) | → **20.9** |

**20.9 mm behind the glass** is what the case has to clear. An earlier draft
borrowed the DevKit's 16.3 figure for the panel too, giving 21.5 — so the
measurement moved the number by 0.6 and confirmed the assumption rather than
overturning it. The panel's pins are simply 0.6 shorter than the DevKit's.

### The mounting holes

Ø3.2 on a **75.90 × 44.80** pitch — 2.90 from each end, 2.60 from each long edge.
Both columns fall in the 6.25 mm bare strips either side of the glass, so the
locating features hide behind the bezel.

The model uses them as **Ø2.9 locating pins** rather than screw bosses. The face
is only 3.6 thick, which is thin to tap, and pins in Ø3.2 holes give 0.3 mm of
location — better than a screw through a clearance hole. Retention is the cover.

## 3. Form

A wedge: vertical back, flat bottom, front face raked **15° from vertical**.

| | Value (mm) |
|---|---|
| Width | 90 |
| Depth | 48 |
| Height | 70 |
| Wall | 2.5 |
| Front face, along the rake | 72.47 — it runs the full height |
| Panel pocket | 82.1 × 50.4, concentric — `aa_off = 0` |

15° puts the screen roughly square to the sightline of someone seated at a desk
about 600 mm away. It is one constant in the model.

### Why a wedge and not the reference lectern

The reference design is an open cabinet: a raked head above a base, with a void
between them. That void is the design — it exists to show the board off. With a
back cover fitted, nothing is on display, so the void becomes an empty room that
is printed and then hidden.

Filling it gives the same front elevation in one continuous volume:

| | Lectern | Wedge |
|---|---|---|
| Overall | ~92 × 65 × 88 | **90 × 48 × 70** |
| Parts | head + base + cover | body + cover |
| Stability | mass high, footprint offset | mass and footprint coincide |
| Print | overhangs at the head/base junction | one orientation |

The wedge is not taller than the panel demands because the deep lower rear lets
the DevKit and the panel's lower edge **occupy the same heights at different
depths**. In a lectern they cannot, which is where its extra 20 mm comes from.

### Why the DevKit is not stacked behind the panel

It would fit. The DevKit's 51.7 × 28.5 footprint sits inside the panel's 81.7 × 50,
and mounting it parallel to the panel would give a much slimmer slab, roughly
90 × 32 × 68. It is rejected on three counts:

1. **RF.** The WROOM's PCB antenna would sit directly behind the display's
   ground plane. This board's entire function is fetching AT's API over WiFi and
   TLS; `docs/hardware-notes.md` already records a first-boot failure caused by
   not waiting for association. Degrading the antenna to save 13 mm is a poor
   trade.
2. **The SD socket.** A 26.6 × 25 block on the back (§2.3), competing for the
   same volume the DevKit would need.
3. **Thermal.** It seals the ESP32 into a 32 mm cavity with no path for air.

Keeping the DevKit low and rearward avoids all three.

## 4. The front face

| Feature | Value (mm) |
|---|---|
| Panel pocket | **82.1 × 50.4** (PCB 81.70 × 50.00 plus 0.4) |
| Aperture | **61.0 × 46.6** — the 57.60 × 43.20 live image plus 1.7 all round |
| Face thickness | **3.6**, matching the glass's proud height |
| Face, along the rake | 72.47 (full height) |
| Bezel, sides | 14.5 |
| Bezel, top and bottom | 12.93 along the rake |
| Panel fixing | four Ø2.9 locating pins on a 75.90 × 44.80 pitch |

**The pocket is offset, the aperture is not.** The active area sits 5.80 off the
PCB's centre (§2.2), so the pocket is shifted 5.80 toward the pin end and the
aperture stays on the case centreline. The image then centres on the case while
the panel does not.

That offset is what sets the width. Centring the aperture needs
`82.1 / 2 + 5.80 + 2.5 = 49.35` either side, so **100 is the narrowest the case
can be** and still keep a 2.5 mm wall past the pocket. The walls come out at
14.75 and 3.15 — an internally lopsided part with a symmetric face, which is the
right way round.

**The panel is handed.** Fitted 180° out, the image lands 11.6 mm off centre.
The pocket cannot prevent it, so the model marks the pin end on the inside of the
back cover, where it is visible during assembly.

The face is 3.6 thick rather than the 2.5 used elsewhere because that is exactly
how far the glass stands proud of the PCB (§2.3). The PCB rests flat on the face's
inner surface, the glass fills the aperture, and its outer surface finishes
**flush with the front of the case** — no rebate, no step, one less tolerance to
carry, and a stiffer panel around the largest hole in the part.

The panel screws to four bosses rather than being trapped by the cover. Its
Ø3.2 holes sit ~2.7 from each edge, putting both columns in the 6.25 dead strips
either side of the glass, so the bosses hide behind the bezel. **The pocket
locates and the screws retain** — registering to the PCB edges rather than to
Ø3.2 holes on M2.5 screws keeps the aperture aligned to the thing that matters.

### The aperture is cut to the live image, with a 1.7 mm margin

Cutting to the glass is not an option in the short axis. The glass is 50.00 wide
on a 50.00 PCB — **edge to edge** — so a glass-sized aperture would leave the
bezel nothing to overlap, and any drift would show a gap past the board rather
than a black border. The aperture has to land between the image and the glass
edge, and the only question is where.

The vertical budget is the 3.40 mm of glass border per side, split between two
competing needs:

| Margin | Aperture | Bezel overlap | Clips after |
|---|---|---|---|
| +1.0 | 59.6 × 45.2 | 2.4 | 1.0 |
| **+1.7** | **61.0 × 46.6** | **1.7** | **1.7** |
| +2.5 | 62.6 × 48.2 | 0.9 | 2.5 |

**1.7 is where the two are equal**, which maximises the worse of them. Against a
±0.4 stack — ±0.2 pocket clearance and ±0.20 PCB outline tolerance — that leaves
1.3 mm of tolerance for the active area being off-centre, and 1.3 mm of overlap
still hiding the glass edge.

That is deliberately more generous than a bezel-to-the-image front would
otherwise be, because **the error is asymmetric**. The board's UI is dark, so a
thin black glass border reads as unlit pixels — the panel was remembered as
edge-to-edge for exactly that reason. Being 1.7 mm generous costs nothing
perceptible; being 1.7 mm tight costs pixels.

The consequence is that the active-area offset no longer has to be measured.
Centred is strongly implied by the datasheet's exact arithmetic (§2.2), and the
margin covers the rest.

The bezel is asymmetric — 14.5 at the sides against 7.7 top and bottom — because
the hardware is. An 81.70 mm PCB carrying a 57.60 mm image has to put the
difference somewhere.

## 5. The DevKit bay

In the deep lower rear, lying horizontally, **component-side-up with the pins
pointing down** into a well.

That orientation is chosen for access: BOOT, EN and the chip all face up, so
lifting the cover puts a finger on them. It deletes the back-panel pinholes, the
mirrored-button arithmetic and the Ø5.5 holes of the superseded design outright.
It is also the shorter of the two: 16.3 + 1.6 + 3.2 = **21.1 mm**, against 24.9
for pins-up.

| | Z above the inner floor (mm) |
|---|---|
| Housing well | 0 → 16.3 |
| PCB | 16.3 → 17.9 |
| Chip, USB-C | 17.9 → 21.1 |

**Supported at the two short ends, not the long edges.** The header rows run
along the long edges with only ~2 mm of PCB outboard of them — not enough to
grip. Two ledges 51.7 apart carry the board, and the cover retains it.

| Feature | Placement |
|---|---|
| USB-C opening | Left flank, 14.0 × 9.0, centred on the connector |
| BOOT / EN | No openings. Accessed by removing the cover |
| Wire route | Down from the housings, turning to run up the inside of the back |

The wire run is longer than a pins-up arrangement would give, because the
housings are at the bottom of the well rather than the top of the board. It is
entirely behind the cover, so it costs nothing but wire.

### This bay sets the case depth, not the panel

The DevKit needs 28.5 mm of depth, pushed to the back wall, while the panel's
back slopes toward it from the front. The two are closest at the **top of the
DevKit stack**, `z = 23.6`, and how close depends on whether the SD socket is
over that point:

| Depth | DevKit spans y | Clear of a plain panel back | Clear of the SD socket |
|---|---|---|---|
| 45 | 14.0 … 42.5 | 2.65 | **0.14** |
| **48** | **17.0 … 45.5** | **5.65** | **3.14** |

At 45 the DevKit misses the SD socket by 0.14 mm, which is not a clearance. The
socket is only 2.6 mm deep, but it sits against a **long** edge of the panel —
in landscape, the top or bottom — and if it is the bottom edge it lands directly
over the bay.

**48 removes the question.** Three millimetres of depth buys 3.14 mm of clearance
whichever long edge the socket is on, so the design does not have to wait on a
measurement to be safe. If it is later confirmed on the upper edge, the case can
go back to 45; that is one constant and nothing else moves.

The panel's own housings are not the binding constraint at 48 — they clear the
back wall by 9.4 mm (§2).

## 6. The cover

One part, snapping over the whole back face. It carries no features except
retention and ventilation, so that every dimension that has to be accurate lives
in the body.

Removing it exposes the DevKit's buttons, both sets of jumper housings and the
panel's back — which is the whole service story: no screws, no pinholes, no
disassembly to reflash.

## 7. Ventilation

Slots in the cover, low at the back and high near the top, so convection carries
heat off the WROOM and out of the panel cavity. `docs/hardware-notes.md` records
no thermal problem in open air over a 225 s capture, but it has never run
enclosed, and the live build holds WiFi and TLS up continuously.

This is cheap insurance in a part that is being modelled anyway. It is not a
measured requirement.

## 8. Printing

Printed on its back face, flat on the bed: the raked front becomes a 15°
overhang from vertical, which needs no support, and the aperture and pocket print
as horizontal features. No part of the body bridges more than the aperture's
70.9 mm span, which is chamfered at its upper edge.

The cover prints flat.

## 9. Production

| Path | Role |
|---|---|
| `models/src/wedge.scad` | The body and cover, one parametric model |
| `models/build/` | Generated STLs, gitignored |
| `models/original/` | The three downloaded STLs, kept with their licence note |

OpenSCAD rather than mesh surgery: this is our part, so it has source. Every
number in §§2–5 is a named constant at the top of the file, and a revised
measurement is a one-line change and a re-run.

Checks in `tests/test_models.py`, run by the existing `pytest.ini`
(`testpaths = tests`):

- The model compiles headlessly and produces a manifold solid.
- Derived clearances — aperture against active area, bay height against the
  21.1 mm stack, depth against the panel's header strip — are asserted in the
  test rather than only commented in the model, so a constant edited in
  isolation cannot silently close a gap.

## 10. `docs/enclosure.md`

Spec §11 says the repo "owes it dimensions rather than a model", naming board
outlines, mounting hole positions and pitch, display active-area offset, and USB
connector position. It has never existed. It is written from §2.1 and §2.2 once
the panel is measured.

The repo now ships a model too, which §11 did not anticipate. `docs/enclosure.md`
records both.

## 11. What is still worth measuring

The panel is in pieces, so both of these are calipers on a bench, unpowered.

| Measurement | Why | If it goes badly |
|---|---|---|
| **Mounting hole centres from the SD end**, and the glass edge from that same end | §2.3 shows a Ø3.2 hole at 2.7 cannot clear a glass edge at 2.60. Something there is not what it looks like | The SD-end bosses are dropped; the panel screws at the pin end and is located by the pocket |
| Panel PCB thickness | Inferred as 1.6 from two measurements agreeing (§2.3), never measured directly | Pocket depth shifts by the difference |
| Which **long edge** the SD socket sits against | §5 sizes the depth to 48 so it does not matter | Confirming it is the upper edge would let the case go back to 45 |

Neither gates the front face. The aperture is set by the active area's 6.25
offset, which your own measurement corroborates: the pin end carrying ~7.15 mm
more bare PCB rules out the glass being centred, and that in turn is what
identifies the drawing's 6.25 as the active area rather than the glass.

One more belongs in `docs/enclosure.md` whenever the board is next assembled and
running — the **active-area offset from each PCB edge**, taken with
`pio run -e esp32_demo -t upload` showing a full-screen fill. It is the
measurement spec §11 named first, and it would confirm the 5.80 offset the whole
front face now depends on.

## 12. Risks

| Risk | Standing |
|---|---|
| The 5.80 active-area offset | Read from the drawing's 6.25 and corroborated indirectly, not measured. It positions the pocket, so getting it wrong shifts the image off centre rather than clipping it — §4's 1.7 mm margin still absorbs 1.3 mm |
| SD-end mounting holes | Geometrically impossible as currently described (§2.3). Bounded: worst case the panel screws at one end only |
| Which long edge the SD sits on | Unconfirmed. §5 sizes the depth so it does not matter; if it is on the upper edge the case could lose 3 mm, but nothing breaks either way |
| Glass proud height | Measured at 3.6, against a datasheet maximum of 4.50 including tape. The face is 3.6 thick to sit the glass flush; a thicker module would stand slightly proud rather than sink, which is the better way to be wrong |
| Thermal, enclosed | Never measured enclosed. Vents are speculative insurance |
| Stability | 90 × 48 footprint under a 70 mm body with a raked face and a cable in the flank. Mass sits low, but it has not been modelled. The foot can be deepened without touching anything else |

## 13. Licence

`models/original/` retains the upstream case and its licence note even though
nothing here derives from it, so the record of what was tried stays complete.
This wedge is the repo's own design and carries the repo's licence.
