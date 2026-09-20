# Enclosure rework: side USB, a deeper bay, and a board sled

Date: 2026-09-20
Status: **superseded** by `2026-09-20-enclosure-wedge-design.md`, not built
Relates to: spec §11 (Enclosure), `docs/hardware-notes.md`

> Kept because §3 is the measurement that justifies the replacement: the DuPont
> stack does not fit this case at any orientation, which is why the enclosure is
> now modelled from scratch rather than reworked. The §2.1 board measurements
> carry forward unchanged into the wedge spec.

## 1. What this covers

The printed case is a third-party model — [Case for 2.8" ILI9341 TFT LCD and
ESP32-C3](https://www.printables.com/model/1822402-case-for-28-ili9341-tft-lcd-and-esp32-c3)
— downloaded as three binary STLs with no CAD source. It was modelled for an
**ESP32-C3**, while this board is a **WROOM-32 DevKit** (`README.md`,
`docs/hardware-notes.md`). Every problem below follows from that one mismatch.

| In | Out |
|---|---|
| Move the USB opening from the back panel to the left wall, and enlarge it | Any change to `Lid.stl` |
| Deepen the Body so the DevKit fits with ordinary jumper wires | Any change to `Platform.stl` |
| A printed sled that holds the DevKit and registers it to the openings | Re-modelling the case from scratch |
| BOOT/EN access holes through the back panel | A revised display mount |
| `docs/enclosure.md`, the dimensions spec §11 has always owed | OTA, so no flashing-free future to design for |

## 2. What the parts actually are

Measured from the meshes on 2026-09-20, not taken from the model's listing.

| Part | Outer (mm) | Notes |
|---|---|---|
| `Body.stl` | 90 × 56 × 20.8 | Walls 1.7 (left/right), 1.6 (rear), 3.6 (front); back panel 2.0 |
| `Lid.stl` | 90 × 56 × 4.5 | 70.9 × 50.4 window; 1.8 mm lip dropping into the Body rebate |
| `Platform.stl` | 13.8 × 47.3 × 90 | A 90 mm extrusion of a leaf profile. No slot captures the case thickness |

Body interior:

| Feature | Value |
|---|---|
| Cavity | 86.6 (X) × 50.8 (Y), floor at Z = 12.4 |
| Lid seat | Z = 29.4, so the bay is **17.0 mm** deep |
| Screw posts | Ø3.5 pilot, Ø4.0 counterbore at Z = 24.4, pitch **76 × 44 mm** |
| Post blocks | Only at `Y 106.6…113.5` and `Y 150.5…157.4` — the middle 37 mm is full width |
| Original USB hole | 11.0 × 7.0 through the 2.0 mm back panel, centred 25.6 mm from the left edge, 17.9 mm from the front |

That the post blocks clear the middle band is what makes a side port possible: a
board centred in Y can slide right up against the left inner wall.

### 2.1 The board, measured

Measured by the owner on 2026-09-20. These supersede every nominal figure that
an earlier draft of this document carried.

| Dimension | Value (mm) |
|---|---|
| PCB length | 51.7 |
| PCB width | 28.5 |
| PCB thickness | 1.6 |
| Envelope, component side (PCB + chip) | 4.8, so the chip stands **3.2** proud |
| Envelope including pins | 12.8, so the pins protrude **8.0** |
| Connector | **USB-C**, protruding 1.6 past the PCB edge |
| BOOT / EN centres | 3.2 from the USB-end edge, 6.35 from the long edge, **mirrored** — one either side of the connector |
| Female DuPont housing | 14.0 long, protruding **8.3 beyond the pin tip** when seated |

Two consequences worth stating plainly:

- **The connector is USB-C, not micro-USB.** `README.md` and
  `docs/hardware-notes.md` identify the USB-serial bridge (CH340, `1A86:7523`)
  but never the connector, and an earlier draft of this spec assumed micro-USB.
  §6 is sized for USB-C.
- The pins are **8.0 mm**, not the 11 mm a full-length header gives. That does
  not change the stack-up, because a DuPont housing seats against the PCB and
  its height above the board is independent of pin length — but 8.0 mm is still
  ample engagement for the contact, which sits low in the housing.

## 3. Why the case has to get deeper

The DevKit does not fit in a 17.0 mm bay with vertical DuPont housings, and no
amount of tolerance work changes that:

| Item | Height (mm) |
|---|---|
| Standoff clearing the chip (3.2, measured) | ≥ 4.0 |
| DevKit PCB (measured) | 1.6 |
| Housing top above the PCB (8.0 pin + 8.3, measured) | 16.3 |
| **Total** | **21.9** |

That is **4.9 mm over** a 17.0 mm bay at the most favourable rail height there is.

Snipping the header pins helps a little, but not enough. The housing does not
bottom out on the header's plastic — it seats against the **pin tip**, so its top
tracks pin length one-for-one. Shortening the pins lowers the stack until the
housing finally does touch the PCB, which happens at 5.7 mm of pin (14.0 − 8.3).
Below that there is no further gain, only lost contact engagement. So the best
snipping can do is 4.0 + 1.6 + 14.0 = **19.6**, still 2.6 over.

The case has to get deeper either way.

`hardware-notes.md` records the panel mounted "pins on the right", so the
display's own header and housings hang the same ~16.3 mm into the bay along the
right short edge — at `X ≈ 203…211`, clear of the DevKit, which ends at 178.

The alternatives were right-angle headers (desoldering 30 pins) or soldering the
nine signals direct. Both were rejected: the stated constraint is that soldering
is not the owner's strength, and the enclosure is the cheaper thing to change
than the electronics.

## 4. Deepening, as a prismatic insert

The Body's cross-section was tested at 1 mm steps from Z = 13 to Z = 25. Across
**Z = 14…24** it is constant to within **0.031 mm**, which is chord error from
tessellating the vertical fillets, not draft. At Z = 25 it diverges by 0.72 mm,
where the Ø4.0 counterbore begins.

So the Body is genuinely prismatic through that band, and deepening is a mesh
operation rather than a modelling exercise:

1. Split every triangle at **Z = 20.0**.
2. Translate everything above the plane by **+12.5 mm**.
3. Bridge the two cut faces by extruding the shared cut polyline 12.5 mm.

Both cut faces have the same cross-section, so the bridge closes exactly and the
result is watertight by construction. No boolean touches the walls, and every
original fillet, rebate, post and counterbore survives unaltered.

| | Before | After |
|---|---|---|
| Body outer | 90 × 56 × 20.8 | 90 × 56 × **33.3** |
| Bay depth | 17.0 | **29.5** |
| Assembled with lid | 23.5 | 36.0 |

**Screws do not change length.** The posts are pilot holes entered from the lid
seat; a longer post is simply a deeper hole, and the screw still engages its top.

12.5 is the smallest insert that clears the measured stack (26.4, §5) with 3.1 mm
left for the jumper wires to turn from vertical to horizontal above the housings.
It is a single constant in the build script.

If 33.3 mm is thicker than you want to live with, snipping the nine pins actually
in use down to 5.7 mm drops the stack 2.3 mm and the insert with it, to 10.2 —
a 31.0 mm case. That is flush cutters rather than a soldering iron, and it is
reversible only by replacing the header. The design does not assume it.

## 5. Board orientation, and why it is upside down

The chosen answer to reflashing is access holes rather than the EN–GND capacitor
that `hardware-notes.md` proposes. BOOT and EN are tactile buttons on the
component side, flanking the USB-C connector on the same short edge — both 3.2
from that edge and 6.35 from a long edge — and they actuate perpendicular to the
PCB. With an 86 × 50 display filling the front, the lid cannot carry access
holes. The only panel that can is the back.

Therefore the DevKit mounts **component-side-down**. Two things fall out of it,
both favourable:

- The header pins protrude from the side opposite the components, so they point
  **up** toward the display — which is where the jumpers want to go anyway, with
  no rework to the board.
- The back panel is already being reworked, since the old rear USB hole becomes
  redundant.

Stack-up in the 29.5 mm bay, measured from the floor at Z = 12.4:

| | Z above floor (mm) |
|---|---|
| Sled rail height, to the PCB's component face | 8.5 |
| Chip hanging down (3.2) | 8.5 → 5.3, clearing the floor by 5.3 |
| PCB (1.6) | 8.5 → 10.1 |
| Pins (8.0) | 10.1 → 18.1 |
| Housing, seated 2.3 above the PCB, top at 16.3 | 12.4 → 26.4 |
| **Clearance to the display at 29.5** | **3.1** |

The 8.5 mm rail height is not slack, and it is not chosen for the chip, which
needs only 3.2. It is set by the USB opening: the opening must be tall enough to
swallow a cable's overmould (§6), and it must still leave solid wall beneath it,
so the connector has to sit high enough off the floor for both to be true at
once. 8.5 is the smallest value that gives a 9.0 mm opening with 2.4 mm of wall
below.

Every figure in that table is now measured. The housing row is the one that had
to be corrected: an earlier draft assumed the housing bottoms out on the PCB and
so put its top at 14.0. It does not — it seats against the pin tip, 2.3 above
the PCB, with its top at 16.3. That is 2.3 mm more than assumed, and it is the
reason the insert in §4 is 12.5 rather than 10.

Clear height above the PCB is **19.4 mm** (29.5 − 8.5 − 1.6) against a 16.3 mm
housing stack.

## 6. The USB opening

Through the left wall (`X = 123.0…124.7`, 1.7 mm thick):

| | Value |
|---|---|
| Size | 14.0 (Y) × 9.0 (Z) |
| Centre | `Y = 132.0` (the cavity's Y centre), `Z = 19.3` |
| Span | `Y 125.0…139.0`, `Z 14.8…23.8` |
| Wall left below the opening | 2.4 mm |
| Top edge | 45° chamfer, so it bridges without support |

The Z centre is derived, not chosen: with the PCB's component face at 8.5 above
the floor (`Z = 20.9`) and the USB-C receptacle standing about 3.2 proud of the
board on the downward-facing side, the connector occupies `Z 17.7…20.9` and its
centreline is `Z = 19.3`.

The opening clears both left post blocks, which end at `Y = 113.5` and begin
again at `Y = 150.5`.

### Why the opening is sized for the cable, not the connector

The connector protrudes 1.6 past the PCB edge. The PCB edge sits at `X = 126.3`,
which puts the receptacle mouth flush with the **inner** wall face at
`X = 124.7` — 1.7 mm back from the outside of the case.

A USB-C plug's shell stands about 6.5 mm proud of its overmould, and seats fully
only when the overmould face reaches the receptacle mouth. With the mouth 1.7 mm
inboard, **the overmould has to enter the opening by 1.7 mm** for the plug to
click home. So the opening must clear the moulded boot, roughly 12 × 7 on a
typical cable, not merely the 8.9 × 3.3 shell.

That is the whole fault being fixed. The original 11 × 7 hole was never tight
for a USB-C shell; it was tight for the boot around it, on the one panel the
stand leans against. 14 × 9 leaves about 1 mm around a typical boot.

The board could instead be pushed 1.6 further left so the connector finishes
flush with the *outside* face, which would need only a shell-sized hole. It is
not, because that puts the PCB edge hard against the inner wall and drags the
BOOT/EN holes to within 0.45 mm of it (§8). Paying for the clearance in opening
size rather than in wall thickness is the cheaper trade.

## 7. The sled

A separate printed part, not geometry moulded into the Body floor.

- Two rails 8.5 mm tall with a 1.8 mm slot (1.6 PCB plus 0.2 clearance), taking
  2.0 mm of each long PCB edge.
- A chamfered 0.6 mm snap lip, so the board presses in from above with the lid off.
- An end stop at the inner end, setting X so the connector lands in the opening.
- Notches for the two **left** post blocks. The right pair sits at `X ≥ 203.1`,
  well beyond the sled, so it needs no relief there.
- Relief under the chip and under both buttons.

**Separate, deliberately.** The sled carries every dimension that could not be
verified from the meshes — board length and width, PCB thickness, button
positions. It is roughly 5 g to reprint against roughly 45 g for the Body, so the
part most likely to need a second attempt is the cheap one.

Resolved against §2.1, with the board centred on `Y = 132.0` and its connector
face flush with the left inner wall:

| Feature | Placement |
|---|---|
| PCB occupies | `X 126.3…178.0`, `Y 117.75…146.25` |
| Rail slots, inner faces | `Y = 117.65` and `Y = 146.35` (28.7 apart, 0.2 total clearance) |
| Rail height | 8.5, slot 1.8, engagement 2.0 per edge |
| End stop | `X = 178.0` |
| Sled footprint | `X 124.7…180.0`, `Y 106.8…157.2` |
| Notches | `X 124.7…136.6` at `Y ≤ 113.7` and `Y ≥ 150.3` |

The sled needs no fasteners. It butts against the left inner wall, which fixes
X; the cavity's long walls fix Y; the post-block notches stop it rotating. The
board's position then follows from the sled alone, which is the point — every
opening in the Body is registered to one part that costs 5 g to reprint.

The rail spacing, slot width and end stop are constants at the top of the build
script, so a revised measurement is a one-line change and a re-run.

## 8. Back panel

| Change | Detail |
|---|---|
| Old USB hole | Plugged flush — 11.0 × 7.0 × 2.0 of material restored |
| BOOT / EN access | Ø5.5 at `X = 129.5`, `Y = 124.1` and `Y = 139.9` |

Both derive from §2.1: 3.2 from the USB-end PCB edge puts them at
`X = 126.3 + 3.2 = 129.5`, and 6.35 from a long edge puts them at
`117.75 + 6.35 = 124.1` and `146.25 − 6.35 = 139.9`. Each hole leaves 2.05 mm of
floor between itself and the left inner wall, and both fall between the sled
rails, so the sled carries matching clearance holes.

The mirroring was confirmed against the board: the two buttons sit on the same
short end, one either side of the USB-C connector, 6.35 from their respective
long edges.

Reach is 10.5 mm (8.5 rail height plus 2.0 panel), so these are pinholes for a
paperclip or a pen tip, in the way a consumer device's reset hole is. Ø5.5 is
chosen over something tighter to absorb error in the measured positions: it
tolerates about ±1.2 mm before a 3 × 4 mm tactile button stops being reachable.

## 9. How the models are produced

| Path | Role |
|---|---|
| `models/original/` | The three downloaded STLs, committed pristine and never edited |
| `models/src/rework.py` | Reads `original/`, writes `build/` |
| `models/build/` | Generated, gitignored |

`numpy` does the prismatic insert, which is exact triangle arithmetic and needs
nothing else. `trimesh` and `manifold3d` do the cuts and unions; both are added
to `requirements-dev.txt`.

Checks live in `tests/test_models.py`, picked up by the existing `pytest.ini`
(`testpaths = tests`) and so run by the same `pytest` invocation as the rest:

- Each output is watertight and has Euler characteristic 2.
- The volume delta matches the intended change to within a tolerance, so a
  boolean that silently removed a wall is caught.
- The untouched cross-section band re-slices identically to the original, so the
  insert is proven not to have disturbed geometry it was not meant to reach.

Keeping `original/` and regenerating means the rework is reviewable as a diff of
intent rather than a diff of 1,558 triangles, and a newer upstream model can be
dropped in and re-run.

## 10. `docs/enclosure.md`

Spec §11 says the repo "owes it dimensions rather than a model", and names board
outlines, mounting hole positions and pitch, display active-area offset, and USB
connector position. That file has never existed. It is written as part of this
work, from the numbers in §2 plus the board measurements from §7.

The repo now also ships a model, which §11 did not anticipate. `docs/enclosure.md`
records both, and notes that the model is a rework of a third-party original
rather than the repo's own design.

## 11. Risks

| Risk | Standing |
|---|---|
| `Platform.stl` fouls the deeper case | Its profile has no slot capturing the case thickness, so it should be unaffected. Inferred from geometry; not confirmed against the printed part |
| The display's own header hang | Assumed to match the DevKit's 16.3, since the same jumpers are used. It sits at `X ≈ 203…211`, clear of the DevKit, which ends at 178 — so it costs depth, not footprint |
| The panel's microSD socket | The 2.8" module carries an SD slot on its back that intrudes into the bay by a couple of millimetres. Its position has not been measured. The DevKit occupies only `X 126…178` of an `X 125…211` bay, so there is somewhere to move to if it fouls |
| Boolean artefacts on a downloaded mesh | Why the volume and watertightness checks exist, and why the wall-deepening avoids booleans altogether |

## 12. Licence

The upstream model's licence governs the reworked parts, and
`models/original/README.md` records it alongside the source URL. Nothing here is
redistributed without that note.
