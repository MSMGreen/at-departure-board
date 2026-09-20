# Display Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the board's entire visual layer in Python — sprites, layout
geometry, and a renderer covering every state — golden-tested, and export the
sprites as C headers the firmware will consume unchanged.

**Architecture:** The display is designed and proven on a laptop before any
hardware is involved. Sprites are authored as pixel-art strings over named
colour *roles*, so one bus sprite recolours itself to whatever the route's
colour is. Layout is pure geometry — integers in, rectangles out, no drawing —
so it is exhaustively testable. The renderer composes the two into a 320x240
image identical to what the ILI9341 will show. An exporter emits the same
sprites as 4bpp indexed C arrays, which is why the simulator and the firmware
cannot drift.

**Tech Stack:** Python 3.10, Pillow 9.4, pytest. No PlatformIO in this plan.

**Spec:** `docs/design/specs/2026-09-06-at-departure-board-design.md`

## Global Constraints

- Screen is exactly **320x240**, portrait-locked landscape, ILI9341.
- Status bar is exactly **18 px** tall; lanes divide the remaining 222 px.
- Watches: **1 to 4**. Lane height is `(240 - 18) // n`.
- Approach-lane horizon is **20 minutes** (`HORIZON_S = 1200`).
- All sprites are **14 rows** tall. Bus is 34 wide, train 48 wide.
- Colour roles are the only way sprites reference colour. No literal RGB in
  sprite data.
- Bus routes have **no** `route_color` from AT; rail does. The palette must
  supply a fallback keyed on kind, and treat `#000000` as absent.
- Golden images are committed and compared **byte-exact**. A render change is a
  deliberate act that updates goldens in the same commit.
- Python only. Nothing in this plan imports Arduino or PlatformIO.

---

## File Structure

```
tools/board/__init__.py     empty, marks the package
tools/board/palette.py      colour roles, kind fallbacks, route_color parsing
tools/board/sprites.py      pixel-art definitions + PIL blitting
tools/board/model.py        Departure / Watch / Board dataclasses
tools/board/layout.py       pure geometry: lanes, vehicle x, text anchors
tools/board/render.py       composes model + layout + sprites -> PIL Image
tools/board/scenes.py       the canonical board states used by goldens and demos
tools/simulate.py           CLI: PNG / GIF / contact sheet
tools/export_sprites.py     writes src/sprites.h (4bpp indexed C arrays)
tests/test_sprites.py
tests/test_palette.py
tests/test_layout.py
tests/test_render.py
tests/test_export.py
tests/golden/*.png
```

`tools/mockup.py` is superseded by this package and is deleted in Task 6.

---

### Task 1: Test harness, palette, and sprite data

**Files:**
- Create: `requirements-dev.txt`, `pytest.ini`
- Create: `tools/board/__init__.py`, `tools/board/palette.py`, `tools/board/sprites.py`
- Test: `tests/test_palette.py`, `tests/test_sprites.py`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `palette.ROLES: dict[str, str]` — role letter → human name
  - `palette.BASE: dict[str, tuple[int,int,int]]` — UI chrome colours
  - `palette.kind_colour(kind: str) -> tuple[int,int,int]`
  - `palette.resolve_badge_colour(kind: str, route_color: str | None) -> tuple[int,int,int]`
  - `sprites.SPRITES: dict[str, Sprite]` keyed `"bus"`, `"train"`
  - `sprites.Sprite` with `.width: int`, `.height: int`, `.rows: list[str]`
  - `sprites.blit(draw, name, x, y, body_colour)` — `y` is the sprite's **bottom** edge

- [ ] **Step 1: Install the dev dependencies**

```bash
python -m pip install pytest==8.3.4 pillow==9.4.0
```

Create `requirements-dev.txt`:

```
pillow==9.4.0
pytest==8.3.4
```

Create `pytest.ini`:

```ini
[pytest]
testpaths = tests
python_files = test_*.py
addopts = -q
```

- [ ] **Step 2: Write the failing palette test**

Create `tests/test_palette.py`:

```python
import pytest
from tools.board import palette


def test_bus_falls_back_because_at_gives_buses_no_colour():
    assert palette.resolve_badge_colour("bus", None) == palette.kind_colour("bus")


def test_rail_colour_from_at_is_honoured():
    # AT's real WEST / E-W green
    assert palette.resolve_badge_colour("train", "97C93D") == (151, 201, 61)


def test_leading_hash_is_accepted():
    assert palette.resolve_badge_colour("train", "#97C93D") == (151, 201, 61)


def test_black_is_treated_as_absent_not_painted():
    # HUIA returns #000000, which is unusable on a dark ground.
    assert palette.resolve_badge_colour("train", "000000") == palette.kind_colour("train")


def test_malformed_colour_falls_back_rather_than_raising():
    assert palette.resolve_badge_colour("bus", "nonsense") == palette.kind_colour("bus")


def test_unknown_kind_falls_back_to_a_neutral():
    assert palette.kind_colour("ferry") == palette.BASE["dim"]
```

- [ ] **Step 3: Run it and confirm it fails**

Run: `python -m pytest tests/test_palette.py -v`
Expected: FAIL, `ModuleNotFoundError: No module named 'tools.board'`

- [ ] **Step 4: Write the palette**

Create `tools/board/__init__.py` as an empty file.

Create `tools/board/palette.py`:

```python
"""Colour roles for the board.

Sprites never name a colour; they name a role. That is what lets one bus sprite
render in whatever colour the route actually is.
"""

# Role letter -> what it means in a sprite grid.
ROLES = {
    ".": "transparent",
    "B": "body",     # the route's colour
    "S": "shade",    # body, darkened - the underside
    "W": "window",
    "D": "dark",     # wheels, bogies
    "L": "light",    # headlight
}

BASE = {
    "bg":       (12, 16, 24),
    "panel":    (22, 29, 42),
    "panel_hi": (32, 42, 60),
    "line":     (44, 57, 78),
    "text":     (233, 238, 245),
    "dim":      (128, 143, 165),
    "live":     (120, 220, 130),
    "warn":     (245, 176, 66),
    "road":     (38, 44, 55),
    "rail":     (58, 50, 44),
    "window":   (210, 240, 255),
    "dark":     (20, 24, 30),
}

# Used when AT supplies no route_color, which for buses is always.
KIND_FALLBACK = {
    "bus":   (0, 168, 224),
    "train": (151, 201, 61),
}


def kind_colour(kind):
    return KIND_FALLBACK.get(kind, BASE["dim"])


def parse_hex(value):
    """'97C93D' or '#97C93D' -> (151, 201, 61). None if unusable."""
    if not value:
        return None
    v = value.strip().lstrip("#")
    if len(v) != 6:
        return None
    try:
        rgb = tuple(int(v[i:i + 2], 16) for i in (0, 2, 4))
    except ValueError:
        return None
    if rgb == (0, 0, 0):
        return None  # HUIA's #000000 - invisible on our ground, treat as absent
    return rgb


def resolve_badge_colour(kind, route_color=None):
    return parse_hex(route_color) or kind_colour(kind)


def shade(rgb, factor=0.55):
    return tuple(max(0, min(255, int(c * factor))) for c in rgb)
```

- [ ] **Step 5: Run the palette test and confirm it passes**

Run: `python -m pytest tests/test_palette.py -v`
Expected: PASS, 6 tests.

- [ ] **Step 6: Write the failing sprite test**

The first test is the safety net for hand-drawn pixel art: it catches any
miscounted row before it ever reaches a renderer.

Create `tests/test_sprites.py`:

```python
import pytest
from tools.board import sprites, palette


@pytest.mark.parametrize("name", ["bus", "train"])
def test_every_row_is_exactly_the_declared_width(name):
    s = sprites.SPRITES[name]
    bad = [(i, len(r)) for i, r in enumerate(s.rows) if len(r) != s.width]
    assert bad == [], f"{name} rows with wrong width (index, len): {bad}"


@pytest.mark.parametrize("name", ["bus", "train"])
def test_row_count_matches_declared_height(name):
    s = sprites.SPRITES[name]
    assert len(s.rows) == s.height


@pytest.mark.parametrize("name", ["bus", "train"])
def test_all_sprites_are_fourteen_rows(name):
    assert sprites.SPRITES[name].height == 14


@pytest.mark.parametrize("name", ["bus", "train"])
def test_only_known_roles_are_used(name):
    s = sprites.SPRITES[name]
    used = {c for row in s.rows for c in row}
    assert used <= set(palette.ROLES), f"unknown roles in {name}: {used - set(palette.ROLES)}"


def test_declared_widths():
    assert sprites.SPRITES["bus"].width == 34
    assert sprites.SPRITES["train"].width == 48


@pytest.mark.parametrize("name", ["bus", "train"])
def test_sprite_has_a_headlight_so_direction_of_travel_reads(name):
    assert any("L" in row for row in sprites.SPRITES[name].rows)


@pytest.mark.parametrize("name", ["bus", "train"])
def test_headlight_is_on_the_right_because_vehicles_travel_rightward(name):
    s = sprites.SPRITES[name]
    xs = [x for row in s.rows for x, c in enumerate(row) if c == "L"]
    assert min(xs) > s.width * 0.75
```

- [ ] **Step 7: Run it and confirm it fails**

Run: `python -m pytest tests/test_sprites.py -v`
Expected: FAIL, `AttributeError` / `ImportError` on `sprites`.

- [ ] **Step 8: Write the sprites**

Create `tools/board/sprites.py`:

```python
"""Pixel-art vehicles, authored as role grids.

Every sprite is 14 rows. Origin when blitting is the BOTTOM-left, because
vehicles sit on a track line and that is the edge that must stay put.
Headlights sit on the right: everything travels left-to-right toward its stop.
"""

from dataclasses import dataclass
from typing import List

from . import palette


@dataclass(frozen=True)
class Sprite:
    width: int
    height: int
    rows: List[str]


BUS = Sprite(34, 14, [
    "...BBBBBBBBBBBBBBBBBBBBBBBBBBBB...",
    "..BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB..",
    ".BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.",
    "BBWWWWBBWWWWBBWWWWBBWWWWBBBBBBBBBB",
    "BBWWWWBBWWWWBBWWWWBBWWWWBBBBBBBBBB",
    "BBWWWWBBWWWWBBWWWWBBWWWWBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBLL",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBLL",
    "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS",
    "....DDDD..........DDDD............",
    "...DDDDDD........DDDDDD...........",
    "...DDDDDD........DDDDDD...........",
    "....DDDD..........DDDD............",
    "..................................",
])

TRAIN = Sprite(48, 14, [
    "...BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB...",
    "..BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB..",
    ".BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.",
    "BBWWWWWWBBWWWWWWBBBBWWWWWWBBWWWWWWBBBBBBBBBBBB",
    "BBWWWWWWBBWWWWWWBBBBWWWWWWBBWWWWWWBBBBBBBBBBBB",
    "BBWWWWWWBBWWWWWWBBBBWWWWWWBBWWWWWWBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBLL",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBLL",
    "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS",
    "....DDDD......DDDD........DDDD......DDDD......",
    "....DDDD......DDDD........DDDD......DDDD......",
    ".....DD........DD..........DD........DD.......",
    "..............................................",
    "..............................................",
])

SPRITES = {"bus": BUS, "train": TRAIN}


def role_colours(body):
    """Map role letters to concrete RGB for one vehicle colour."""
    return {
        "B": body,
        "S": palette.shade(body),
        "W": palette.BASE["window"],
        "D": palette.BASE["dark"],
        "L": palette.BASE["warn"],
    }


def blit(draw, name, x, y, body):
    """Draw sprite `name` with its BOTTOM-left corner at (x, y)."""
    s = SPRITES[name]
    colours = role_colours(body)
    top = int(y) - s.height
    x = int(x)
    for ry, row in enumerate(s.rows):
        py = top + ry
        run_start, run_role = None, None
        for rx in range(s.width + 1):
            role = row[rx] if rx < s.width else None
            if role != run_role:
                if run_role in colours and run_start is not None:
                    draw.rectangle(
                        (x + run_start, py, x + rx - 1, py),
                        fill=colours[run_role],
                    )
                run_start, run_role = rx, role
```

The nested loop coalesces horizontal runs into single rectangles rather than
setting 476 individual pixels — the same run-length approach the firmware will
use to push spans to the TFT.

- [ ] **Step 9: Run the sprite tests and confirm they pass**

Run: `python -m pytest tests/test_sprites.py -v`
Expected: PASS, 13 tests.

If `test_every_row_is_exactly_the_declared_width` fails, the pixel art above has
a miscounted row — fix the row, not the test. The test is the reason the error
is caught here rather than as a visual glitch later.

- [ ] **Step 10: Commit**

```bash
git add requirements-dev.txt pytest.ini tools/board/ tests/test_palette.py tests/test_sprites.py
git commit -m "Add colour roles and pixel-art vehicle sprites

Sprites name roles, not colours, so one bus sprite renders in whatever
colour the route is. Row-width tests catch miscounted pixel art at author
time rather than as a visual glitch.

Black is treated as absent: HUIA's #000000 would be invisible on our ground."
```

---

### Task 2: Board model

**Files:**
- Create: `tools/board/model.py`
- Test: `tests/test_model.py`

**Interfaces:**
- Consumes: `palette.resolve_badge_colour`.
- Produces:
  - `model.Departure(eta_s: int, live: bool = False, cancelled: bool = False, scheduled: str = "")`
  - `model.Departure.is_departed -> bool`
  - `model.Watch(badge, headsign, kind, departures, route_color=None)`
  - `model.Watch.colour -> tuple[int,int,int]`
  - `model.Watch.next -> Departure | None`
  - `model.Watch.following -> Departure | None`
  - `model.Board(watches: list[Watch], clock: str, stale_s: int = 0, dimmed: bool = False)`
  - `model.Board.is_stale -> bool`

- [ ] **Step 1: Write the failing test**

Create `tests/test_model.py`:

```python
from tools.board.model import Board, Departure, Watch
from tools.board import palette


def w(**kw):
    base = dict(badge="20", headsign="to Wynyard Quarter", kind="bus",
                departures=[Departure(240), Departure(1020)])
    base.update(kw)
    return Watch(**base)


def test_next_and_following_are_the_first_two():
    x = w()
    assert x.next.eta_s == 240
    assert x.following.eta_s == 1020


def test_next_is_none_when_nothing_is_scheduled():
    x = w(departures=[])
    assert x.next is None
    assert x.following is None


def test_following_is_none_when_only_one_departure():
    x = w(departures=[Departure(240)])
    assert x.next.eta_s == 240
    assert x.following is None


def test_departures_are_ordered_by_eta_regardless_of_input_order():
    x = w(departures=[Departure(900), Departure(120), Departure(400)])
    assert [d.eta_s for d in x.departures] == [120, 400, 900]


def test_a_bus_takes_the_fallback_colour():
    assert w().colour == palette.kind_colour("bus")


def test_rail_takes_ats_colour_when_present():
    x = w(kind="train", route_color="97C93D")
    assert x.colour == (151, 201, 61)


def test_departed_is_negative_eta():
    assert Departure(-5).is_departed
    assert not Departure(0).is_departed


def test_board_is_stale_only_past_the_threshold():
    assert not Board([w()], "17:42", stale_s=0).is_stale
    assert not Board([w()], "17:42", stale_s=89).is_stale
    assert Board([w()], "17:42", stale_s=91).is_stale


def test_board_rejects_more_than_four_watches():
    import pytest
    with pytest.raises(ValueError):
        Board([w()] * 5, "17:42")


def test_board_rejects_zero_watches():
    import pytest
    with pytest.raises(ValueError):
        Board([], "17:42")
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `python -m pytest tests/test_model.py -v`
Expected: FAIL, `ModuleNotFoundError: No module named 'tools.board.model'`

- [ ] **Step 3: Write the model**

Create `tools/board/model.py`:

```python
"""What the screen is showing, independent of how it is drawn.

This mirrors the struct the firmware builds after merging schedule with
realtime, so the simulator and the device agree on vocabulary.
"""

from dataclasses import dataclass, field
from typing import List, Optional

from . import palette

STALE_AFTER_S = 90
MAX_WATCHES = 4


@dataclass
class Departure:
    eta_s: int
    live: bool = False
    cancelled: bool = False
    scheduled: str = ""

    @property
    def is_departed(self):
        return self.eta_s < 0


@dataclass
class Watch:
    badge: str
    headsign: str
    kind: str
    departures: List[Departure] = field(default_factory=list)
    route_color: Optional[str] = None

    def __post_init__(self):
        self.departures = sorted(self.departures, key=lambda d: d.eta_s)

    @property
    def colour(self):
        return palette.resolve_badge_colour(self.kind, self.route_color)

    @property
    def next(self):
        return self.departures[0] if self.departures else None

    @property
    def following(self):
        return self.departures[1] if len(self.departures) > 1 else None


@dataclass
class Board:
    watches: List[Watch]
    clock: str
    stale_s: int = 0
    dimmed: bool = False

    def __post_init__(self):
        if not 1 <= len(self.watches) <= MAX_WATCHES:
            raise ValueError(
                f"board needs 1..{MAX_WATCHES} watches, got {len(self.watches)}"
            )

    @property
    def is_stale(self):
        return self.stale_s > STALE_AFTER_S
```

- [ ] **Step 4: Run it and confirm it passes**

Run: `python -m pytest tests/test_model.py -v`
Expected: PASS, 10 tests.

- [ ] **Step 5: Commit**

```bash
git add tools/board/model.py tests/test_model.py
git commit -m "Add board model

Mirrors the struct the firmware builds after the schedule/realtime merge,
so simulator and device share vocabulary. Departures self-sort; a board
outside 1..4 watches is rejected at construction."
```

---

### Task 3: Layout geometry

The heart of the design and the part worth testing hardest: no drawing, just
integers in and rectangles out.

**Files:**
- Create: `tools/board/layout.py`
- Test: `tests/test_layout.py`

**Interfaces:**
- Consumes: `sprites.SPRITES` (for widths only).
- Produces:
  - `layout.W = 320`, `layout.H = 240`, `layout.STATUS_H = 18`, `layout.HORIZON_S = 1200`
  - `layout.Rect` — `NamedTuple(x0, y0, x1, y1)` with `.width`, `.height`
  - `layout.lane_rects(n: int) -> list[Rect]`
  - `layout.Lane` — `NamedTuple(rect, badge, headsign_xy, minutes_xy, following_xy, track, marker_x, sprite_baseline)`
  - `layout.lane(index: int, n: int) -> Lane`
  - `layout.vehicle_x(eta_s: int, lane: Lane, kind: str) -> int`

- [ ] **Step 1: Write the failing test**

Create `tests/test_layout.py`:

```python
import pytest
from tools.board import layout


# --- lane division -------------------------------------------------------

@pytest.mark.parametrize("n,expected", [(1, 222), (2, 111), (3, 74), (4, 55)])
def test_lane_height_divides_the_area_below_the_status_bar(n, expected):
    assert layout.lane_rects(n)[0].height == expected


@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_lanes_start_below_the_status_bar(n):
    assert layout.lane_rects(n)[0].y0 == layout.STATUS_H


@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_lanes_never_overflow_the_screen(n):
    assert layout.lane_rects(n)[-1].y1 <= layout.H


@pytest.mark.parametrize("n", [2, 3, 4])
def test_lanes_do_not_overlap(n):
    rects = layout.lane_rects(n)
    for a, b in zip(rects, rects[1:]):
        assert a.y1 <= b.y0


@pytest.mark.parametrize("n", [0, 5])
def test_lane_count_outside_one_to_four_is_rejected(n):
    with pytest.raises(ValueError):
        layout.lane_rects(n)


# --- vehicle position: the whole point of the design ---------------------

def test_at_the_horizon_the_vehicle_is_at_the_far_left():
    ln = layout.lane(0, 2)
    assert layout.vehicle_x(layout.HORIZON_S, ln, "bus") == ln.track.x0


def test_at_zero_the_vehicle_has_arrived_at_the_marker():
    ln = layout.lane(0, 2)
    from tools.board import sprites
    expected = ln.marker_x - sprites.SPRITES["bus"].width
    assert layout.vehicle_x(0, ln, "bus") == expected


def test_beyond_the_horizon_clamps_to_the_far_left():
    ln = layout.lane(0, 2)
    assert layout.vehicle_x(99999, ln, "bus") == ln.track.x0


def test_after_departure_clamps_to_the_marker():
    ln = layout.lane(0, 2)
    assert layout.vehicle_x(-600, ln, "bus") == layout.vehicle_x(0, ln, "bus")


def test_half_the_horizon_is_about_half_the_track():
    ln = layout.lane(0, 2)
    lo = layout.vehicle_x(layout.HORIZON_S, ln, "bus")
    hi = layout.vehicle_x(0, ln, "bus")
    mid = layout.vehicle_x(layout.HORIZON_S // 2, ln, "bus")
    assert abs(mid - (lo + hi) // 2) <= 1


def test_position_advances_monotonically_as_time_runs_down():
    ln = layout.lane(0, 3)
    xs = [layout.vehicle_x(s, ln, "train") for s in range(1200, -1, -30)]
    assert xs == sorted(xs)


def test_a_train_is_wider_so_it_stops_further_left_than_a_bus():
    ln = layout.lane(0, 2)
    assert layout.vehicle_x(0, ln, "train") < layout.vehicle_x(0, ln, "bus")


# --- lane internals ------------------------------------------------------

@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_sprite_baseline_sits_on_the_track(n):
    ln = layout.lane(0, n)
    assert ln.sprite_baseline == ln.track.y0


@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_track_stops_short_of_the_marker(n):
    ln = layout.lane(0, n)
    assert ln.track.x1 <= ln.marker_x


@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_everything_stays_inside_its_lane(n):
    ln = layout.lane(0, n)
    for name, xy in [("headsign", ln.headsign_xy), ("minutes", ln.minutes_xy),
                     ("following", ln.following_xy)]:
        assert ln.rect.y0 <= xy[1] <= ln.rect.y1, f"{name} escaped lane"


@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_the_widest_sprite_always_fits_the_track(n):
    from tools.board import sprites
    ln = layout.lane(0, n)
    assert ln.track.width >= sprites.SPRITES["train"].width


def test_lane_index_selects_the_right_band():
    assert layout.lane(2, 4).rect.y0 == layout.lane_rects(4)[2].y0
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `python -m pytest tests/test_layout.py -v`
Expected: FAIL, `ModuleNotFoundError: No module named 'tools.board.layout'`

- [ ] **Step 3: Write the layout**

Create `tools/board/layout.py`:

```python
"""Pure geometry for the approach-lane layout. No drawing happens here.

The one idea worth stating: a vehicle's x position IS its time to arrival.
It enters at the left at HORIZON_S and touches the stop marker at zero, so the
board can be read from across a room without resolving any digits.
"""

from typing import List, NamedTuple

from . import sprites

W, H = 320, 240
STATUS_H = 18
HORIZON_S = 20 * 60

MARGIN = 4          # gap from screen edge to lane card
PAD = 6             # gap from card edge to content
MARKER_INSET = 58   # distance from right edge to the stop marker
TRACK_LIFT = 12     # track height above the card's bottom edge


class Rect(NamedTuple):
    x0: int
    y0: int
    x1: int
    y1: int

    @property
    def width(self):
        return self.x1 - self.x0

    @property
    def height(self):
        return self.y1 - self.y0


class Lane(NamedTuple):
    rect: Rect
    badge: Rect
    headsign_xy: tuple
    minutes_xy: tuple
    following_xy: tuple
    track: Rect
    marker_x: int
    sprite_baseline: int


def lane_rects(n) -> List[Rect]:
    if not 1 <= n <= 4:
        raise ValueError(f"board supports 1..4 lanes, got {n}")
    avail = H - STATUS_H
    lh = avail // n
    return [Rect(0, STATUS_H + i * lh, W, STATUS_H + (i + 1) * lh) for i in range(n)]


def lane(index, n) -> Lane:
    r = lane_rects(n)[index]
    badge_h = 18
    badge = Rect(r.x0 + 10, r.y0 + PAD, r.x0 + 10 + 34, r.y0 + PAD + badge_h)

    track_y = r.y1 - TRACK_LIFT
    marker_x = W - MARKER_INSET
    track = Rect(r.x0 + 10, track_y, marker_x, track_y + 2)

    return Lane(
        rect=r,
        badge=badge,
        headsign_xy=(badge.x1 + 8, r.y0 + PAD + 2),
        minutes_xy=(W - 12, r.y0 + PAD),
        following_xy=(W - 12, r.y0 + PAD + 22),
        track=track,
        marker_x=marker_x,
        sprite_baseline=track_y,
    )


def vehicle_x(eta_s, lane, kind):
    """Map seconds-until-arrival to an x position on the lane's track.

    HORIZON_S or more -> the left end. Zero or less -> touching the marker.
    """
    sw = sprites.SPRITES[kind].width
    x_start = lane.track.x0
    x_stop = lane.marker_x - sw
    clamped = max(0, min(int(eta_s), HORIZON_S))
    progress = 1.0 - clamped / HORIZON_S
    return int(round(x_start + progress * (x_stop - x_start)))
```

- [ ] **Step 4: Run it and confirm it passes**

Run: `python -m pytest tests/test_layout.py -v`
Expected: PASS, 24 tests.

If `test_the_widest_sprite_always_fits_the_track` fails, `MARKER_INSET` is too
large — reduce it until a 48px train fits with room to travel.

- [ ] **Step 5: Commit**

```bash
git add tools/board/layout.py tests/test_layout.py
git commit -m "Add approach-lane geometry

Pure integers in, rectangles out, so the core idea - that a vehicle's x
position is its time to arrival - is exhaustively testable without
rendering anything. Position is monotonic and clamps at both ends."
```

---

### Task 4: Renderer and canonical scenes

**Files:**
- Create: `tools/board/render.py`, `tools/board/scenes.py`
- Test: `tests/test_render.py`

**Interfaces:**
- Consumes: `model.Board`, `layout.lane`, `sprites.blit`, `palette.BASE`.
- Produces:
  - `render.render(board: Board, t: float = 0.0) -> PIL.Image.Image` — always 320x240 RGB
  - `scenes.SCENES: dict[str, Board]` with keys `two_up`, `four_up`, `single`,
    `stale`, `cancelled`, `empty`, `arriving`, `dimmed`

- [ ] **Step 1: Write the failing test**

Create `tests/test_render.py`:

```python
import pytest
from tools.board import render, scenes
from tools.board.model import Board, Departure, Watch


ALL = sorted(scenes.SCENES)


@pytest.mark.parametrize("name", ALL)
def test_every_scene_renders_at_the_panel_size(name):
    assert render.render(scenes.SCENES[name]).size == (320, 240)


@pytest.mark.parametrize("name", ALL)
def test_rendering_is_deterministic(name):
    a = render.render(scenes.SCENES[name], t=0.0)
    b = render.render(scenes.SCENES[name], t=0.0)
    assert a.tobytes() == b.tobytes()


@pytest.mark.parametrize("name", ALL)
def test_nothing_renders_as_an_empty_frame(name):
    img = render.render(scenes.SCENES[name])
    assert len(img.getcolors(maxcolors=100000)) > 4


def test_animation_time_actually_changes_the_image():
    b = scenes.SCENES["two_up"]
    assert render.render(b, t=0.0).tobytes() != render.render(b, t=0.7).tobytes()


def test_a_dimmed_board_is_strictly_darker_than_a_live_one():
    def lum(img):
        px = list(img.convert("L").getdata())
        return sum(px) / len(px)
    assert lum(render.render(scenes.SCENES["dimmed"])) < \
           lum(render.render(scenes.SCENES["two_up"]))


def test_a_stale_board_still_draws_its_vehicles():
    # Losing WiFi must not blank the board - it must look different, not dead.
    stale = render.render(scenes.SCENES["stale"])
    assert len(stale.getcolors(maxcolors=100000)) > 8


def test_an_empty_watch_renders_without_raising():
    b = Board([Watch("20", "to Wynyard Quarter", "bus", [])], "01:12")
    assert render.render(b).size == (320, 240)


def test_a_departed_vehicle_does_not_escape_the_lane():
    b = Board([Watch("20", "to Wynyard Quarter", "bus", [Departure(-90)])], "17:42")
    assert render.render(b).size == (320, 240)
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `python -m pytest tests/test_render.py -v`
Expected: FAIL, `ModuleNotFoundError: No module named 'tools.board.render'`

- [ ] **Step 3: Write the scenes**

Create `tools/board/scenes.py`:

```python
"""The canonical board states. Goldens, demos and the firmware's DEMO_MODE all
draw from this one list, so a state can never be exercised in one and forgotten
in another.

Values are the real ones observed at stop 8213 and Kingsland station on
2026-09-06; see docs/at-api-notes.md.
"""

from .model import Board, Departure, Watch

BUS20 = dict(badge="20", headsign="to Wynyard Quarter", kind="bus")
TRAIN = dict(badge="E-W", headsign="to Britomart", kind="train",
             route_color="97C93D")
BUS22 = dict(badge="22R", headsign="to City Centre", kind="bus")
ONEHUNGA = dict(badge="O-W", headsign="to Onehunga", kind="train",
                route_color="00AEEF")


def _two():
    return [
        Watch(departures=[Departure(240, live=True), Departure(1020)], **BUS20),
        Watch(departures=[Departure(420, live=True), Departure(1320, live=True)], **TRAIN),
    ]


SCENES = {
    "single": Board(
        [Watch(departures=[Departure(240, live=True), Departure(1020)], **BUS20)],
        "17:42"),

    "two_up": Board(_two(), "17:42"),

    "four_up": Board([
        Watch(departures=[Departure(240, live=True), Departure(1020)], **BUS20),
        Watch(departures=[Departure(420, live=True), Departure(1320, live=True)], **TRAIN),
        Watch(departures=[Departure(660, live=True), Departure(1860)], **BUS22),
        Watch(departures=[Departure(1140), Departure(2940)], **ONEHUNGA),
    ], "17:42"),

    # WiFi has been gone for four minutes. Data stays up, marked.
    "stale": Board(_two(), "17:46", stale_s=240),

    # Seven minutes early is real: observed delay was -427s.
    "arriving": Board([
        Watch(departures=[Departure(15, live=True), Departure(1020)], **BUS20),
        Watch(departures=[Departure(420, live=True), Departure(1320, live=True)], **TRAIN),
    ], "17:45"),

    "cancelled": Board([
        Watch(departures=[Departure(300, live=True, cancelled=True),
                          Departure(1200, live=True)], **BUS20),
        Watch(departures=[Departure(420, live=True), Departure(1320, live=True)], **TRAIN),
    ], "17:42"),

    # 1am. Nothing left tonight. This is the state most boards get wrong.
    "empty": Board([
        Watch(departures=[], **BUS20),
        Watch(departures=[], **TRAIN),
    ], "01:12"),

    "dimmed": Board(_two(), "22:30", dimmed=True),
}
```

- [ ] **Step 4: Write the renderer**

Create `tools/board/render.py`:

```python
"""Compose model + layout + sprites into the 320x240 frame.

Mirrors what the firmware does per lane, so what you see here is what the
ILI9341 shows.
"""

import math
import os

from PIL import Image, ImageDraw, ImageFont

from . import layout, palette, sprites
from .model import Board

FONT_DIR = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")
_cache = {}

DIM_FACTOR = 0.45


def _font(name, size):
    key = (name, size)
    if key not in _cache:
        try:
            _cache[key] = ImageFont.truetype(os.path.join(FONT_DIR, name), size)
        except OSError:
            _cache[key] = ImageFont.load_default()
    return _cache[key]


def bold(s):
    return _font("arialbd.ttf", s)


def reg(s):
    return _font("arial.ttf", s)


def mono(s):
    return _font("consolab.ttf", s)


def _mins(eta_s):
    """Seconds to the number actually shown. Never shows a negative."""
    return max(0, int(eta_s) // 60)


def _status_bar(d, board):
    d.rectangle((0, 0, layout.W, layout.STATUS_H), fill=palette.BASE["panel"])
    mid = layout.STATUS_H / 2
    d.text((6, mid), "Kingsland", font=reg(11), fill=palette.BASE["dim"], anchor="lm")
    d.text((layout.W - 6, mid), board.clock, font=mono(12),
           fill=palette.BASE["text"], anchor="rm")

    if board.is_stale:
        label, colour = f"stale {board.stale_s // 60}m", palette.BASE["warn"]
    else:
        label, colour = "live", palette.BASE["live"]
    d.text((layout.W - 54, mid), label, font=reg(10),
           fill=palette.BASE["dim"], anchor="rm")
    dx = layout.W - 46
    d.ellipse((dx - 3, mid - 3, dx + 3, mid + 3), fill=colour)


def _lane(d, ln, watch, t, index):
    card = palette.BASE["panel"] if index % 2 == 0 else palette.BASE["panel_hi"]
    d.rounded_rectangle(
        (ln.rect.x0 + layout.MARGIN, ln.rect.y0 + 3,
         ln.rect.x1 - layout.MARGIN, ln.rect.y1 - 3),
        radius=5, fill=card)

    nxt = watch.next
    colour = watch.colour

    # route badge
    f = bold(13)
    label = watch.badge
    bw = int(d.textlength(label, font=f) + 14)
    b = ln.badge
    d.rounded_rectangle((b.x0, b.y0, b.x0 + bw, b.y1), radius=4, fill=colour)
    d.text(((b.x0 + b.x0 + bw) / 2, (b.y0 + b.y1) / 2), label, font=f,
           fill=palette.BASE["dark"], anchor="mm")

    d.text((b.x0 + bw + 8, ln.headsign_xy[1]), watch.headsign, font=reg(10),
           fill=palette.BASE["dim"])

    # times
    if nxt is None:
        d.text(ln.minutes_xy, "--", font=mono(20), fill=palette.BASE["dim"], anchor="ra")
        d.text(ln.following_xy, "none tonight", font=reg(9),
               fill=palette.BASE["dim"], anchor="ra")
    else:
        txt_colour = palette.BASE["dim"] if nxt.cancelled else palette.BASE["text"]
        d.text(ln.minutes_xy, str(_mins(nxt.eta_s)), font=mono(20),
               fill=txt_colour, anchor="ra")
        if nxt.cancelled:
            box = d.textbbox(ln.minutes_xy, str(_mins(nxt.eta_s)),
                             font=mono(20), anchor="ra")
            y = (box[1] + box[3]) // 2
            d.line((box[0] - 2, y, box[2] + 2, y), fill=palette.BASE["warn"], width=2)
        following = watch.following
        d.text(ln.following_xy,
               f"then {_mins(following.eta_s)}" if following else "then --",
               font=reg(9), fill=palette.BASE["dim"], anchor="ra")

    # track + stop marker
    track_colour = palette.BASE["road"] if watch.kind == "bus" else palette.BASE["rail"]
    d.rectangle(ln.track, fill=track_colour)
    d.rectangle((ln.marker_x, ln.track.y0 - 12, ln.marker_x + 2, ln.track.y1),
                fill=palette.BASE["dim"])
    d.ellipse((ln.marker_x - 3, ln.track.y0 - 17, ln.marker_x + 5, ln.track.y0 - 9),
              fill=colour)

    if nxt is None:
        # Parked at the left, lights off.
        sprites.blit(d, watch.kind, ln.track.x0, ln.sprite_baseline,
                     palette.shade(colour, 0.4))
        return

    x = layout.vehicle_x(nxt.eta_s, ln, watch.kind)
    bob = math.sin(t * 5 + index * 1.7) if nxt.eta_s > 30 else 0
    body = palette.shade(colour, 0.4) if nxt.cancelled else colour
    sprites.blit(d, watch.kind, x, ln.sprite_baseline + bob, body)


def render(board: Board, t: float = 0.0) -> Image.Image:
    img = Image.new("RGB", (layout.W, layout.H), palette.BASE["bg"])
    d = ImageDraw.Draw(img)
    _status_bar(d, board)
    n = len(board.watches)
    for i, watch in enumerate(board.watches):
        _lane(d, layout.lane(i, n), watch, t, i)
    if board.dimmed:
        img = Image.eval(img, lambda v: int(v * DIM_FACTOR))
    return img
```

- [ ] **Step 5: Run the render tests and confirm they pass**

Run: `python -m pytest tests/test_render.py -v`
Expected: PASS, 28 tests.

- [ ] **Step 6: Run the whole suite**

Run: `python -m pytest -v`
Expected: PASS, all tests from Tasks 1-4.

- [ ] **Step 7: Commit**

```bash
git add tools/board/render.py tools/board/scenes.py tests/test_render.py
git commit -m "Add board renderer and canonical scenes

Scenes are the single list of states that goldens, demos and the firmware's
DEMO_MODE all draw from, so a state cannot be exercised in one and
forgotten in another. Includes the two states boards usually get wrong:
1am with nothing left, and a cancelled service."
```

---

### Task 5: Golden images

**Files:**
- Create: `tools/regolden.py`
- Create: `tests/golden/*.png` (generated)
- Modify: `tests/test_render.py` (append)

**Interfaces:**
- Consumes: `render.render`, `scenes.SCENES`.
- Produces: `tools/regolden.py` as a CLI; goldens committed under `tests/golden/`.

- [ ] **Step 1: Write the regolden tool**

Create `tools/regolden.py`:

```python
#!/usr/bin/env python3
"""Regenerate the committed golden images.

Run this ONLY when a render change is intended, and commit the resulting
PNGs in the same commit as the change that caused them.

    python tools/regolden.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.board import render, scenes  # noqa: E402

GOLDEN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "tests", "golden")


def main():
    os.makedirs(GOLDEN, exist_ok=True)
    for name in sorted(scenes.SCENES):
        path = os.path.join(GOLDEN, name + ".png")
        render.render(scenes.SCENES[name], t=0.0).save(path)
        print("wrote", os.path.normpath(path))


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Append the golden test to `tests/test_render.py`**

```python
import os
from PIL import Image

GOLDEN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden")


@pytest.mark.parametrize("name", ALL)
def test_matches_golden(name):
    path = os.path.join(GOLDEN, name + ".png")
    assert os.path.exists(path), (
        f"no golden for '{name}'. If this scene is new, run "
        f"`python tools/regolden.py` and commit the result."
    )
    expected = Image.open(path).convert("RGB")
    actual = render.render(scenes.SCENES[name], t=0.0)
    assert actual.tobytes() == expected.tobytes(), (
        f"render of '{name}' changed. If that was intended, run "
        f"`python tools/regolden.py` and commit the new goldens."
    )
```

- [ ] **Step 3: Run it and confirm it fails**

Run: `python -m pytest tests/test_render.py -k golden -v`
Expected: FAIL on every scene, "no golden for ...".

- [ ] **Step 4: Generate the goldens**

Run: `python tools/regolden.py`
Expected: eight lines, one per scene.

- [ ] **Step 5: Inspect them before trusting them**

Open `tests/golden/four_up.png` and `tests/golden/empty.png`. A golden is only
worth having if it was correct when frozen. Check specifically:

- four lanes fit without overlapping and nothing is clipped at 55px
- the empty board reads as "nothing tonight", not as a broken board
- the cancelled time is struck through and legibly greyed
- in `arriving`, the bus is at the marker, not past it

If any is wrong, fix the renderer and regenerate before committing.

- [ ] **Step 6: Confirm the goldens now pass**

Run: `python -m pytest -v`
Expected: PASS, all tests.

- [ ] **Step 7: Commit**

```bash
git add tools/regolden.py tests/golden tests/test_render.py
git commit -m "Freeze golden images for every board state

Byte-exact comparison, so an unintended render change fails loudly rather
than drifting. Changing a render means regenerating goldens in the same
commit, which the failure message says explicitly."
```

---

### Task 6: Simulator CLI

**Files:**
- Create: `tools/simulate.py`
- Delete: `tools/mockup.py`
- Test: `tests/test_simulate.py`

**Interfaces:**
- Consumes: `render.render`, `scenes.SCENES`.
- Produces:
  - `simulate.contact_sheet(names: list[str], scale: int = 2) -> Image.Image`
  - `simulate.animate(board, frames: int = 48, fps: int = 12) -> list[Image.Image]`
  - CLI: `python tools/simulate.py [--scene NAME] [--gif] [--all] [--scale N] [--show]`

- [ ] **Step 1: Write the failing test**

Create `tests/test_simulate.py`:

```python
import pytest
from tools import simulate
from tools.board import scenes


def test_contact_sheet_is_wider_than_one_panel():
    sheet = simulate.contact_sheet(["two_up", "four_up"], scale=1)
    assert sheet.width > 320 * 2


def test_contact_sheet_of_one_scene_still_works():
    assert simulate.contact_sheet(["single"], scale=1).width >= 320


def test_animate_returns_the_requested_frame_count():
    assert len(simulate.animate(scenes.SCENES["two_up"], frames=12)) == 12


def test_animation_frames_are_not_all_identical():
    frames = simulate.animate(scenes.SCENES["two_up"], frames=12)
    assert len({f.tobytes() for f in frames}) > 1


def test_unknown_scene_name_is_rejected_clearly():
    with pytest.raises(KeyError):
        simulate.contact_sheet(["nope"], scale=1)
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `python -m pytest tests/test_simulate.py -v`
Expected: FAIL, `ModuleNotFoundError: No module named 'tools.simulate'`

- [ ] **Step 3: Write the simulator**

Create `tools/simulate.py`:

```python
#!/usr/bin/env python3
"""Preview the board without hardware.

    python tools/simulate.py --all --scale 2 --show
    python tools/simulate.py --scene arriving --gif
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.board import render, scenes  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")


def _label_font(size=15):
    try:
        return ImageFont.truetype(
            os.path.join(os.environ.get("WINDIR", "C:/Windows"),
                         "Fonts", "arialbd.ttf"), size)
    except OSError:
        return ImageFont.load_default()


def contact_sheet(names, scale=2):
    """Every named scene side by side, NEAREST-upscaled to show real pixels."""
    imgs = [(n, render.render(scenes.SCENES[n])) for n in names]  # KeyError if unknown
    pad, lab = 12, 22
    cw, ch = 320 * scale, 240 * scale
    sheet = Image.new("RGB", (pad + len(imgs) * (cw + pad), pad + lab + ch + pad),
                      (18, 18, 20))
    d = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(imgs):
        x = pad + i * (cw + pad)
        sheet.paste(im.resize((cw, ch), Image.NEAREST), (x, pad + lab))
        d.text((x, pad + 4), name, font=_label_font(), fill=(235, 235, 240))
    return sheet


def animate(board, frames=48, fps=12):
    return [render.render(board, t=i / fps) for i in range(frames)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", default=None, help="scene name, or omit for --all")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--gif", action="store_true")
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--show", action="store_true")
    args = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    names = sorted(scenes.SCENES) if (args.all or not args.scene) else [args.scene]
    if args.scene and args.scene not in scenes.SCENES:
        raise SystemExit(
            f"unknown scene {args.scene!r}. known: {', '.join(sorted(scenes.SCENES))}")

    sheet = contact_sheet(names, args.scale)
    sheet_path = os.path.join(OUT, "scenes.png")
    sheet.save(sheet_path)
    print("wrote", sheet_path)

    if args.gif:
        for n in names:
            frames = [f.resize((640, 480), Image.NEAREST)
                      for f in animate(scenes.SCENES[n])]
            p = os.path.join(OUT, f"{n}.gif")
            frames[0].save(p, save_all=True, append_images=frames[1:],
                           duration=80, loop=0, optimize=True)
            print("wrote", p)

    if args.show:
        sheet.show()


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `python -m pytest tests/test_simulate.py -v`
Expected: PASS, 5 tests.

- [ ] **Step 5: Look at the result**

Run: `python tools/simulate.py --all --gif`
Open `tools/out/scenes.png` and `tools/out/arriving.gif`.

- [ ] **Step 6: Delete the superseded mockup**

```bash
git rm tools/mockup.py
```

`tools/board/` plus `tools/simulate.py` replace it entirely. Leaving both would
create exactly the drift this package exists to prevent.

- [ ] **Step 7: Run the whole suite**

Run: `python -m pytest -v`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add tools/simulate.py tests/test_simulate.py
git commit -m "Add simulator CLI, retire mockup.py

Renders any scene as a contact sheet or GIF without hardware. mockup.py is
deleted rather than left alongside - two renderers is the drift this
package exists to prevent."
```

---

### Task 7: Export sprites to C

**Files:**
- Create: `tools/export_sprites.py`
- Create: `src/sprites.h` (generated)
- Test: `tests/test_export.py`

**Interfaces:**
- Consumes: `sprites.SPRITES`, `palette.ROLES`.
- Produces:
  - `export_sprites.ROLE_INDEX: dict[str, int]` — role letter → 0..5, `.` is 0
  - `export_sprites.pack(sprite) -> bytes` — 4bpp, two pixels per byte, high nibble first
  - `export_sprites.render_header() -> str`
  - CLI writing `src/sprites.h`

Sprites are exported as **4-bit indexed role data**, not RGB565. Two reasons:
the firmware recolours a sprite per route at draw time, which fixed RGB would
prevent; and 34x14 at 4bpp is 238 bytes against 952 for RGB565.

- [ ] **Step 1: Write the failing test**

Create `tests/test_export.py`:

```python
import pytest
from tools import export_sprites as ex
from tools.board import sprites, palette


def test_transparent_is_index_zero():
    assert ex.ROLE_INDEX["."] == 0


def test_every_role_has_an_index():
    assert set(ex.ROLE_INDEX) == set(palette.ROLES)


def test_indices_fit_in_a_nibble():
    assert all(0 <= v <= 15 for v in ex.ROLE_INDEX.values())


@pytest.mark.parametrize("name", ["bus", "train"])
def test_packed_size_is_two_pixels_per_byte(name):
    s = sprites.SPRITES[name]
    assert len(ex.pack(s)) == (s.width + 1) // 2 * s.height


@pytest.mark.parametrize("name", ["bus", "train"])
def test_pack_round_trips_back_to_the_original_grid(name):
    s = sprites.SPRITES[name]
    data = ex.pack(s)
    rev = {v: k for k, v in ex.ROLE_INDEX.items()}
    stride = (s.width + 1) // 2
    for y, row in enumerate(s.rows):
        for x, ch in enumerate(row):
            byte = data[y * stride + x // 2]
            nib = (byte >> 4) if x % 2 == 0 else (byte & 0x0F)
            assert rev[nib] == ch, f"{name} mismatch at ({x},{y})"


def test_header_declares_both_sprites():
    h = ex.render_header()
    assert "SPRITE_BUS_DATA" in h
    assert "SPRITE_TRAIN_DATA" in h


def test_header_declares_dimensions_matching_python():
    h = ex.render_header()
    assert f"SPRITE_BUS_W {sprites.SPRITES['bus'].width}" in h
    assert f"SPRITE_TRAIN_W {sprites.SPRITES['train'].width}" in h
    assert "SPRITE_H 14" in h


def test_header_has_an_include_guard():
    h = ex.render_header()
    assert "#pragma once" in h


def test_header_warns_against_hand_editing():
    assert "generated" in ex.render_header().lower()


def test_role_indices_are_exported_for_the_firmware_palette():
    h = ex.render_header()
    assert "ROLE_BODY" in h and "ROLE_WINDOW" in h
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `python -m pytest tests/test_export.py -v`
Expected: FAIL, `ModuleNotFoundError: No module named 'tools.export_sprites'`

- [ ] **Step 3: Write the exporter**

Create `tools/export_sprites.py`:

```python
#!/usr/bin/env python3
"""Export the Python sprites as 4bpp indexed C arrays.

Indexed rather than RGB565 so the firmware can recolour a sprite to the
route's own colour at draw time, and because 34x14 at 4bpp is 238 bytes
against 952.

    python tools/export_sprites.py     # writes src/sprites.h
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.board import palette, sprites  # noqa: E402

ROLE_INDEX = {".": 0, "B": 1, "S": 2, "W": 3, "D": 4, "L": 5}

ROLE_CONST = {
    ".": "ROLE_NONE", "B": "ROLE_BODY", "S": "ROLE_SHADE",
    "W": "ROLE_WINDOW", "D": "ROLE_DARK", "L": "ROLE_LIGHT",
}

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "src", "sprites.h")


def pack(sprite):
    """4bpp, two pixels per byte, high nibble is the left pixel."""
    out = bytearray()
    for row in sprite.rows:
        for x in range(0, sprite.width, 2):
            hi = ROLE_INDEX[row[x]]
            lo = ROLE_INDEX[row[x + 1]] if x + 1 < sprite.width else 0
            out.append((hi << 4) | lo)
    return bytes(out)


def _array(name, data, per_line=12):
    lines = []
    for i in range(0, len(data), per_line):
        chunk = ", ".join(f"0x{b:02X}" for b in data[i:i + per_line])
        lines.append("    " + chunk + ",")
    body = "\n".join(lines)
    return f"static const uint8_t {name}[{len(data)}] = {{\n{body}\n}};"


def render_header():
    parts = [
        "// generated by tools/export_sprites.py - do not edit by hand",
        "// regenerate after changing tools/board/sprites.py",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "// 4bpp indexed. Two pixels per byte, high nibble leftmost.",
        "// Index 0 is transparent; the rest are roles the UI maps to colours,",
        "// which is what lets one sprite render in any route's colour.",
    ]
    for letter, const in ROLE_CONST.items():
        parts.append(f"#define {const} {ROLE_INDEX[letter]}")
    parts.append("")
    parts.append(f"#define SPRITE_H {sprites.SPRITES['bus'].height}")

    for name in ("bus", "train"):
        s = sprites.SPRITES[name]
        up = name.upper()
        parts += [
            "",
            f"#define SPRITE_{up}_W {s.width}",
            f"#define SPRITE_{up}_STRIDE {(s.width + 1) // 2}",
            _array(f"SPRITE_{up}_DATA", pack(s)),
        ]
    return "\n".join(parts) + "\n"


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(render_header())
    print("wrote", os.path.normpath(OUT), f"({os.path.getsize(OUT)} bytes)")


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `python -m pytest tests/test_export.py -v`
Expected: PASS, 12 tests.

The round-trip test is the important one: it proves the nibble packing the
firmware will unpack is byte-for-byte the art drawn in Python.

- [ ] **Step 5: Generate the header and read it**

Run: `python tools/export_sprites.py`
Expected: `wrote src\sprites.h (about 2 KB)`

Open `src/sprites.h`. Confirm `SPRITE_BUS_W 34`, `SPRITE_H 14`, and two arrays
of 238 and 336 bytes respectively.

- [ ] **Step 6: Run the whole suite**

Run: `python -m pytest -v`
Expected: PASS, everything.

- [ ] **Step 7: Commit**

```bash
git add tools/export_sprites.py src/sprites.h tests/test_export.py
git commit -m "Export sprites as 4bpp indexed C arrays

Indexed rather than RGB565 so the firmware can recolour per route at draw
time, and because 34x14 at 4bpp is 238 bytes against 952. A round-trip test
proves the nibbles the firmware unpacks are the art drawn in Python."
```

---

### Task 8: Document the display layer

**Files:**
- Create: `README.md`
- Modify: `.gitignore`

- [ ] **Step 1: Confirm generated output is ignored but `src/sprites.h` is not**

`tools/out/` must stay ignored; `src/sprites.h` is committed because the
firmware build depends on it and must not require Python.

Run: `git check-ignore -v tools/out/scenes.png` → expect a match on `tools/out/`
Run: `git check-ignore -v src/sprites.h` → expect **no** output (exit 1)

- [ ] **Step 2: Write the README**

Create `README.md`:

````markdown
# AT Departure Board

An ESP32 + 2.8" TFT that shows when the next bus and train actually leave,
using Auckland Transport's realtime feed. Each service gets a lane, and the
vehicle's position along its lane is its time to arrival — it pulls into the
stop as the countdown reaches zero, so the board reads from across a room
without resolving any digits.

Status: display layer complete and testable without hardware. Firmware next.

## Try it without hardware

```bash
python -m pip install -r requirements-dev.txt
python tools/simulate.py --all --gif --show
```

Renders every board state to `tools/out/`, including the two that are easiest
to get wrong: 1am with nothing left tonight, and a cancelled service.

## Development

```bash
python -m pytest              # everything
python tools/regolden.py      # ONLY when a render change is intended
python tools/export_sprites.py # regenerate src/sprites.h after editing sprites
```

Renders are compared byte-exact against `tests/golden/`. A failing golden test
means the render changed — if that was intended, regenerate and commit the
goldens in the same commit.

## Layout

| Concern | File |
|---|---|
| Colour roles, AT `route_color` handling | `tools/board/palette.py` |
| Pixel-art vehicles | `tools/board/sprites.py` |
| What the screen is showing | `tools/board/model.py` |
| Lane geometry, ETA → x position | `tools/board/layout.py` |
| Drawing | `tools/board/render.py` |
| The canonical board states | `tools/board/scenes.py` |

## Documentation

- `docs/at-api-notes.md` — the AT API as it actually behaves, verified against
  live endpoints. Read this before touching the network code; it differs from
  AT's own documentation in five places.
- `docs/design/specs/` — design.
- `docs/design/plans/` — implementation plans.

## Licence

MIT.
````

- [ ] **Step 3: Verify the README's commands actually work**

Run each command in the README from a clean shell. A README that lies is worse
than no README.

Run: `python -m pytest`
Run: `python tools/simulate.py --all`
Run: `python tools/export_sprites.py`

- [ ] **Step 4: Commit**

```bash
git add README.md .gitignore
git commit -m "Document the display layer

Every command in the README was run before committing it."
```

---

## Self-Review

**Spec coverage.** §7 (screen) is Tasks 3-5. §3's colour handling is Task 1.
§8's stale, cancelled and empty states are scenes in Task 4 and frozen in
Task 5. §9's `tools/mockup.py` sprite-exporter role is Task 7. §10's repo shape
is Task 8.

Deliberately **not** in this plan, and belonging to the firmware plan: §2
wiring, §4 modules other than `ui`, §5 data flow, §6 stop resolution, §8
brightness and quiet hours as runtime behaviour, §11 enclosure dimensions
(needs the hardware in hand), §12 fixture re-capture after 13 September.

**Placeholder scan.** No TBDs. Every code step carries runnable code; every test
step carries real assertions.

**Type consistency.** `Sprite.width/height/rows` used identically in Tasks 1, 3
and 7. `Lane.track` is a `Rect` everywhere. `vehicle_x(eta_s, lane, kind)` keeps
its signature in Tasks 3 and 4. `ROLE_INDEX` in Task 7 matches `palette.ROLES`
from Task 1, and a test asserts it.

**Known risk.** The pixel art in Task 1 is hand-counted. Step 9's width test
exists precisely to catch that at author time; if it fails, the art is wrong,
not the test.
