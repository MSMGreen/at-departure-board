# Theme System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the board's whole look selectable — palette, vehicle sprites and
background scenery — starting with two themes built properly rather than six
built thinly.

**Architecture:** A `Theme` is data: colours, a sprite set, a role→meaning map,
and a small scenery function. Sprites come in two size classes because a 40px
vehicle cannot fit a 55px lane — `large` for 1–2 watches, `compact` for 3–4.
The art itself is **already committed** as parametric source
(`tools/author_art.py` draws it; `tools/board/art/sprites_data.py` is
generated), so no task in this plan retypes pixel grids.

**Tech Stack:** Python 3.10, Pillow 9.4, pytest. No firmware in this plan.

**Spec:** `docs/superpowers/specs/2026-09-06-at-departure-board-design.md` §7.

## Why this replaces the earlier six-theme version

The first draft of this plan had six themes at 34x16 with six flat colour
roles. Rendered, they were mediocre — every theme shared one silhouette and
only the palette changed. Three things were wrong and are fixed here:

1. **Size.** 18px was self-imposed. At two lanes there is a 75px vertical
   budget; the art now uses ~40px.
2. **Roles.** 4bpp allows 16 indices; six were used. Eleven now, including
   highlight, midshadow, glint and outline — that is where depth comes from.
3. **Empty lanes.** A vehicle alone in a 111px lane looks abandoned. Each
   theme now draws ~15 lines of scenery — stars and hills, or a city skyline.

Scope narrowed from six themes to two, because six average themes are worth
less than two good ones. The registry is unchanged in shape, so a third theme
is one file and no rework.

## Global Constraints

- Screen 320x240. Status bar 18px. Lanes `(240-18)//n`, n in 1..4.
- **Two size classes.** `large` when n ≤ 2, `compact` when n ≥ 3.
  - large: ≤ 44px tall, ≤ 100px wide
  - compact: ≤ 18px tall
  Height caps are enforced by test. Compact's 18px is the hard geometric limit:
  at n=4 the lane is 55px, the badge ends at `y0+24` and the track sits at
  `y0+43`.
- Roles are `. B H S M W G D K L A`. What each means is per-theme.
- **Only `transit` honours AT's `route_color`.** Ghibli uses its own palette.
- Scenery draws **only** in the `large` size class. There is no room at 3–4
  lanes and it would fight the text.
- Art is never typed into a file by hand. Change `tools/author_art.py` and
  regenerate.

---

## File Structure

```
tools/author_art.py                 parametric art source        [EXISTS]
tools/board/art/sprites_data.py     generated art                [EXISTS]
tools/board/art/__init__.py                                      [EXISTS]
tools/board/themes/__init__.py      registry
tools/board/themes/base.py          Theme dataclass + role resolution
tools/board/themes/transit.py
tools/board/themes/ghibli.py
tools/board/scenery.py              per-theme background drawing
tests/test_themes.py                cross-theme suite
```

Modified: `palette.py`, `sprites.py`, `layout.py`, `render.py`, `model.py`,
`simulate.py`, `regolden.py`, `export_sprites.py`, and the existing tests.

---

### Task 1: Theme type and registry, compact art only

A pure refactor. Deliberately does **not** introduce large art, so the eight
existing goldens must pass unregenerated — that is the proof the refactor
changed nothing.

**Files:**
- Create: `tools/board/themes/__init__.py`, `base.py`, `transit.py`
- Modify: `tools/board/palette.py`, `sprites.py`, `render.py`, `model.py`
- Modify: `tests/test_palette.py`, `tests/test_model.py`
- Delete: `tests/test_sprites.py`

**Interfaces:**
- Produces:
  - `themes.base.Theme(name, label, colours, sprites, kind_fallback, roles, use_route_color, scenery=None)`
    where `sprites` is `dict[(size, kind) -> Sprite]`
  - `Theme.colour(key)`, `Theme.badge_colour(kind, route_color=None)`
  - `Theme.role_colours(body) -> dict[str, tuple]`
  - `Theme.sprite(size, kind) -> Sprite`
  - `themes.get(name)`, `themes.names()`, `themes.all_themes()`, `themes.DEFAULT = "transit"`
  - `palette.ROLES` (now 11), `parse_hex`, `shade`, `bright`
  - `sprites.blit(draw, sprite, x, y, colours)` — **changed**: sprite object + resolved colours
  - `render.render(board, t=0.0, theme=None)`
  - `model.Board.theme: str = "transit"`

- [ ] **Step 1: Extend the role vocabulary**

In `tools/board/palette.py`, replace `ROLES` and add `bright`:

```python
ROLES = {
    ".": "transparent",
    "B": "body",        # the vehicle's colour
    "H": "highlight",   # body, lit from above
    "S": "shade",       # body, darkened
    "M": "midshadow",   # the underside
    "W": "window",
    "G": "glint",       # specular dot on glass
    "D": "outline",
    "K": "detail",      # wheels, pupils, ironwork
    "L": "lamp",
    "A": "accent",      # trim, roof, destination blind
}


def bright(rgb, factor=1.6, floor=40):
    """Brighten toward a glow. Neon-style themes make their halo from this."""
    return tuple(min(255, int(c * factor) + floor) for c in rgb)
```

Delete `BASE`, `KIND_FALLBACK`, `kind_colour`, `resolve_badge_colour` — they
become per-theme.

- [ ] **Step 2: Write the failing theme test**

Create `tests/test_themes.py`:

```python
import pytest

from tools.board import themes


def test_transit_is_the_default():
    assert themes.DEFAULT == "transit"
    assert themes.DEFAULT in themes.names()


def test_get_returns_a_theme_with_its_own_name():
    assert themes.get("transit").name == "transit"


def test_unknown_theme_names_what_is_available():
    with pytest.raises(KeyError) as e:
        themes.get("nope")
    assert "transit" in str(e.value)


def test_missing_colour_key_names_the_theme():
    with pytest.raises(KeyError) as e:
        themes.get("transit").colour("no_such_colour")
    assert "transit" in str(e.value)


def test_role_colours_resolve_against_the_body():
    cols = themes.get("transit").role_colours((100, 150, 200))
    assert cols["B"] == (100, 150, 200)
    assert cols["S"] != cols["B"]
    assert cols["H"] != cols["B"]


def test_transit_honours_at_route_colour():
    assert themes.get("transit").badge_colour("train", "97C93D") == (151, 201, 61)


def test_sprites_are_keyed_by_size_and_kind():
    t = themes.get("transit")
    assert t.sprite("compact", "bus").width == 36
    assert t.sprite("compact", "train").width == 48
```

- [ ] **Step 3: Run it and confirm it fails**

Run: `python -m pytest tests/test_themes.py -v`
Expected: FAIL, `ModuleNotFoundError: No module named 'tools.board.themes'`

- [ ] **Step 4: Write the Theme type**

Create `tools/board/themes/base.py`:

```python
"""A theme is data: colours, sprites, what each role means, and a scene."""

from dataclasses import dataclass
from typing import Callable, Dict, Optional, Tuple

from .. import palette
from ..sprites import Sprite


@dataclass(frozen=True)
class Theme:
    name: str
    label: str
    colours: Dict[str, tuple]
    sprites: Dict[tuple, Sprite]          # (size, kind) -> Sprite
    kind_fallback: Dict[str, tuple]
    roles: Dict[str, object]              # tuple, or "shade"/"bright"/"body"
    use_route_color: bool = False
    scenery: Optional[Callable] = None    # (draw, rect, kind, seed) -> None

    def colour(self, key) -> Tuple[int, int, int]:
        try:
            return self.colours[key]
        except KeyError:
            raise KeyError(f"theme {self.name!r} has no colour {key!r}") from None

    def badge_colour(self, kind, route_color=None):
        if self.use_route_color:
            parsed = palette.parse_hex(route_color)
            if parsed:
                return parsed
        return self.kind_fallback.get(kind, self.colour("dim"))

    def role_colours(self, body):
        out = {"B": body}
        for role, value in self.roles.items():
            if value == "body":
                out[role] = body
            elif value == "shade":
                out[role] = palette.shade(body)
            elif value == "bright":
                out[role] = palette.bright(body)
            else:
                out[role] = value
        return out

    def sprite(self, size, kind) -> Sprite:
        try:
            return self.sprites[(size, kind)]
        except KeyError:
            raise KeyError(
                f"theme {self.name!r} has no {size}/{kind} sprite") from None
```

- [ ] **Step 5: Write the art loader**

Create `tools/board/art/__init__.py` (replacing the empty file):

```python
"""Load generated sprite data into Sprite objects.

The data is produced by tools/author_art.py. Never edit sprites_data.py.
"""

from ..sprites import Sprite
from .sprites_data import SPRITES as _RAW


def load(theme):
    """All sprites for one theme, keyed (size, kind)."""
    out = {}
    for (th, size, kind), (w, h, rows) in _RAW.items():
        if th == theme:
            out[(size, kind)] = Sprite(w, h, rows)
    if not out:
        raise KeyError(f"no generated art for theme {theme!r}")
    return out


def themes_with_art():
    return sorted({t for (t, _, _) in _RAW})
```

- [ ] **Step 6: Write the transit theme**

Create `tools/board/themes/transit.py`:

```python
"""The default look: a transit board. The only theme that honours AT's own
route colours, because it is the only one pretending to be signage."""

from ..art import load
from .base import Theme

COLOURS = {
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
    "window":   (214, 242, 255),
    "dark":     (20, 24, 30),
}

THEME = Theme(
    name="transit",
    label="Transit",
    colours=COLOURS,
    sprites=load("transit"),
    kind_fallback={"bus": (0, 168, 224), "train": (151, 201, 61)},
    roles={"H": "bright", "S": "shade", "M": "shade",
           "W": (214, 242, 255), "G": (255, 255, 255),
           "D": (10, 20, 30), "K": (16, 26, 36),
           "L": (250, 200, 70), "A": (0, 140, 190)},
    use_route_color=True,
)
```

- [ ] **Step 7: Write the registry**

Create `tools/board/themes/__init__.py`:

```python
"""Theme registry. Adding a theme means adding a module and one import."""

from .base import Theme  # noqa: F401
from . import transit

DEFAULT = "transit"

_ALL = [transit.THEME]
THEMES = {t.name: t for t in _ALL}


def names():
    return list(THEMES)


def get(name):
    try:
        return THEMES[name]
    except KeyError:
        raise KeyError(
            f"unknown theme {name!r}; available: {', '.join(sorted(THEMES))}"
        ) from None


def all_themes():
    return [THEMES[n] for n in sorted(THEMES)]
```

- [ ] **Step 8: Change `sprites.blit`**

In `tools/board/sprites.py` delete `BUS`, `TRAIN`, `SPRITES` and
`role_colours`. Keep `Sprite`, and change `blit`:

```python
def blit(draw, sprite, x, y, colours):
    """Draw `sprite` with its BOTTOM-left corner at (x, y).

    `colours` is a resolved role->RGB dict from Theme.role_colours(). Roles
    absent from it are skipped, which is how transparency works.
    """
    top = int(y) - sprite.height
    x = int(x)
    for ry, row in enumerate(sprite.rows):
        py = top + ry
        run_start, run_role = None, None
        for rx in range(sprite.width + 1):
            role = row[rx] if rx < sprite.width else None
            if role != run_role:
                if run_role in colours and run_start is not None:
                    draw.rectangle((x + run_start, py, x + rx - 1, py),
                                   fill=colours[run_role])
                run_start, run_role = rx, role
```

- [ ] **Step 9: Thread the theme through `model` and `render`**

Add `theme: str = "transit"` to `Board`. Delete `Watch.colour`.

In `tools/board/render.py`, import `themes`, drop the `palette.BASE` uses:

```python
def render(board: Board, t: float = 0.0, theme=None) -> Image.Image:
    th = theme if theme is not None else themes.get(board.theme)
    img = Image.new("RGB", (layout.W, layout.H), th.colour("bg"))
    d = ImageDraw.Draw(img)
    _status_bar(d, board, th)
    n = len(board.watches)
    for i, watch in enumerate(board.watches):
        _lane(d, layout.lane(i, n), watch, t, i, th)
    if board.dimmed:
        img = Image.eval(img, lambda v: int(v * DIM_FACTOR))
    return img
```

`_status_bar(d, board, th)` and `_lane(d, ln, watch, t, index, th)` take the
theme. Inside `_lane`, every `palette.BASE[k]` becomes `th.colour(k)`, plus:

```python
    colour = th.badge_colour(watch.kind, watch.route_color)
    sprite = th.sprite("compact", watch.kind)          # Task 3 makes this dynamic
    ...
    x = layout.vehicle_x(nxt.eta_s, ln, sprite.width)
    sprites.blit(d, sprite, x, ln.sprite_baseline + bob, th.role_colours(body))
```

In `tests/test_model.py`, delete `test_a_bus_takes_the_fallback_colour` and
`test_rail_takes_ats_colour_when_present`. In `tests/test_palette.py`, delete
everything referencing `BASE`/`kind_colour` and keep `parse_hex` coverage:

```python
def test_leading_hash_is_accepted():
    assert palette.parse_hex("#97C93D") == (151, 201, 61)


def test_black_is_treated_as_absent():
    assert palette.parse_hex("000000") is None


def test_malformed_is_none():
    assert palette.parse_hex("nonsense") is None


def test_bright_moves_away_from_black():
    assert palette.bright((10, 10, 10)) > (10, 10, 10)
```

Delete `tests/test_sprites.py` — Task 2 replaces it with the cross-theme suite.

- [ ] **Step 10: Run everything. The goldens must pass unregenerated**

Run: `python -m pytest -v`
Expected: PASS, including all 8 existing goldens **without** running
`regolden.py`. If a golden fails, the refactor changed rendering — diff it,
do not regenerate. This is the only regression check this task has.

Note `layout.vehicle_x` already takes a width from the display-layer work, so
no change is needed there.

- [ ] **Step 11: Commit**

```bash
git add tools/board/themes tools/board/art tools/board/palette.py \
        tools/board/sprites.py tools/board/render.py tools/board/model.py \
        tests/test_themes.py tests/test_palette.py tests/test_model.py
git rm tests/test_sprites.py
git commit -m "Extract the current look into a transit theme

Pure refactor, compact art only. The eight existing goldens pass
unregenerated, which is the proof that nothing about rendering changed.

Role vocabulary grows from 6 to 11 - highlight, midshadow, glint, accent -
because 4bpp allows 16 and flat fills were why the old sprites looked flat."
```

---

### Task 2: The cross-theme test suite

**Files:**
- Modify: `tests/test_themes.py`

- [ ] **Step 1: Add the suite**

Append to `tests/test_themes.py`:

```python
from tools.board import palette

REQUIRED_COLOURS = ["bg", "panel", "panel_hi", "line", "text", "dim",
                    "live", "warn", "road", "rail", "window", "dark"]
ALL_THEMES = [t.name for t in themes.all_themes()]
SIZES = {"large": 44, "compact": 18}
MAX_W = 100


def _lum(rgb):
    def ch(c):
        c = c / 255
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (ch(c) for c in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = _lum(a), _lum(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


@pytest.mark.parametrize("name", ALL_THEMES)
def test_every_required_colour_is_present(name):
    t = themes.get(name)
    assert [k for k in REQUIRED_COLOURS if k not in t.colours] == []


@pytest.mark.parametrize("name", ALL_THEMES)
def test_has_all_four_sprites(name):
    t = themes.get(name)
    for size in SIZES:
        for kind in ("bus", "train"):
            assert t.sprite(size, kind) is not None


@pytest.mark.parametrize("name", ALL_THEMES)
def test_sprite_rows_match_declared_width(name):
    t = themes.get(name)
    for (size, kind), s in t.sprites.items():
        bad = [(i, len(r)) for i, r in enumerate(s.rows) if len(r) != s.width]
        assert bad == [], f"{name}/{size}/{kind}: {bad}"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_sprites_respect_their_size_class(name):
    # compact's 18px is geometric: at n=4 the lane is 55px, the badge ends at
    # y0+24 and the track sits at y0+43.
    t = themes.get(name)
    for (size, kind), s in t.sprites.items():
        assert s.height <= SIZES[size], f"{name}/{size}/{kind} is {s.height}px"
        assert s.width <= MAX_W, f"{name}/{size}/{kind} is {s.width}px wide"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_large_art_actually_uses_its_budget(name):
    # If large art is not meaningfully bigger than compact, the size class is
    # pointless and someone forgot to author it.
    t = themes.get(name)
    for kind in ("bus", "train"):
        assert t.sprite("large", kind).height >= t.sprite("compact", kind).height + 12


@pytest.mark.parametrize("name", ALL_THEMES)
def test_only_known_roles_are_used(name):
    t = themes.get(name)
    for (size, kind), s in t.sprites.items():
        used = {c for row in s.rows for c in row}
        assert used <= set(palette.ROLES), f"{name}/{size}/{kind}"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_every_role_used_resolves_to_a_colour(name):
    t = themes.get(name)
    cols = t.role_colours((120, 120, 120))
    for (size, kind), s in t.sprites.items():
        used = {c for row in s.rows for c in row} - {"."}
        assert not (used - set(cols)), f"{name}/{size}/{kind}: {used - set(cols)}"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_there_is_a_light_at_the_leading_edge(name):
    # Direction of travel must read. This asserts a light EXISTS at the front,
    # not that lights appear only there - the ghibli train is lantern-lit
    # along its whole length by design.
    t = themes.get(name)
    for (size, kind), s in t.sprites.items():
        xs = [x for row in s.rows for x, c in enumerate(row) if c == "L"]
        assert xs, f"{name}/{size}/{kind} has no light"
        assert max(xs) >= s.width - 4, f"{name}/{size}/{kind} light not at front"


# --- legibility ----------------------------------------------------------
# If one of these fails, change the theme's colours, never the threshold.

@pytest.mark.parametrize("name", ALL_THEMES)
def test_body_text_is_legible_on_the_panel(name):
    t = themes.get(name)
    assert contrast(t.colour("text"), t.colour("panel")) >= 4.5


@pytest.mark.parametrize("name", ALL_THEMES)
def test_secondary_text_is_legible_on_the_panel(name):
    t = themes.get(name)
    assert contrast(t.colour("dim"), t.colour("panel")) >= 2.5


@pytest.mark.parametrize("name", ALL_THEMES)
def test_badge_ink_is_legible_on_every_badge_colour(name):
    t = themes.get(name)
    for kind, fill in t.kind_fallback.items():
        assert contrast(t.colour("dark"), fill) >= 3.0, f"{name}/{kind}"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_alternating_lanes_are_distinguishable(name):
    t = themes.get(name)
    assert contrast(t.colour("panel"), t.colour("panel_hi")) >= 1.05
```

- [ ] **Step 2: Run it**

Run: `python -m pytest tests/test_themes.py -v`
Expected: PASS. `test_large_art_actually_uses_its_budget` passes because
transit's large art (38px) already exists in the generated data.

- [ ] **Step 3: Commit**

```bash
git add tests/test_themes.py
git commit -m "Add cross-theme validation suite

Parametrised over every registered theme, so a new theme inherits the whole
suite by existing. Includes WCAG contrast checks - a theme that is lovely
in a contact sheet and unreadable from the hallway is a failed theme - and
a check that large art actually uses its extra budget rather than being
compact art in a bigger box."
```

---

### Task 3: Size classes

**Files:**
- Modify: `tools/board/layout.py`, `tools/board/render.py`
- Modify: `tests/test_layout.py`, `tests/test_render.py`
- Regenerate: `tests/golden/*.png`

**Interfaces:**
- Produces: `layout.size_class(n) -> str`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_layout.py`:

```python
@pytest.mark.parametrize("n,expected", [(1, "large"), (2, "large"),
                                        (3, "compact"), (4, "compact")])
def test_size_class_follows_lane_count(n, expected):
    assert layout.size_class(n) == expected


def test_large_art_fits_a_two_lane_board():
    # lane 111px: badge ends at y0+24, track at y1-12. 40px art must clear it.
    ln = layout.lane(0, 2)
    assert ln.sprite_baseline - 44 > ln.badge.y1


def test_compact_art_fits_a_four_lane_board():
    ln = layout.lane(0, 4)
    assert ln.sprite_baseline - 18 > ln.badge.y1
```

- [ ] **Step 2: Run and confirm it fails**

Run: `python -m pytest tests/test_layout.py -k size_class -v`
Expected: FAIL, `AttributeError: module 'tools.board.layout' has no attribute 'size_class'`

- [ ] **Step 3: Implement**

In `tools/board/layout.py`:

```python
LARGE_MAX_LANES = 2


def size_class(n):
    """Which sprite set fits n lanes.

    At 3+ lanes the lane is 74px or less and large art collides with the
    badge, so the compact set is not a preference - it is the only thing
    that fits.
    """
    return "large" if n <= LARGE_MAX_LANES else "compact"
```

In `tools/board/render.py`, `render()` computes it once and passes it down:

```python
    n = len(board.watches)
    size = layout.size_class(n)
    for i, watch in enumerate(board.watches):
        _lane(d, layout.lane(i, n), watch, t, i, th, size)
```

and `_lane(d, ln, watch, t, index, th, size)` uses
`sprite = th.sprite(size, watch.kind)`.

- [ ] **Step 4: Run the layout tests**

Run: `python -m pytest tests/test_layout.py -v`
Expected: PASS.

- [ ] **Step 5: Regenerate goldens — they legitimately change now**

Unlike Task 1, this task *is* meant to change rendering: every 1–2 lane scene
now draws large art.

Run: `python tools/regolden.py`

Then open `tests/golden/two_up.png`, `single.png`, `arriving.png`,
`cancelled.png`, `empty.png` and check:

- the large bus clears the badge and the "then N" text
- at `arriving` (15s) the vehicle sits at the marker, not past it
- `four_up.png` is unchanged — it uses compact art
- nothing is clipped at the lane edges

- [ ] **Step 6: Run everything**

Run: `python -m pytest -v`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add tools/board/layout.py tools/board/render.py tests/test_layout.py \
        tests/golden
git commit -m "Pick sprite size from lane count

Large art at 1-2 lanes, compact at 3-4. Not a preference: at three lanes
the lane is 74px and 40px art collides with the badge.

Goldens regenerate here by design - unlike the Task 1 refactor, this task
is meant to change what is drawn."
```

---

### Task 4: Scenery

The change that stops a 111px lane looking abandoned.

**Files:**
- Create: `tools/board/scenery.py`
- Modify: `tools/board/render.py`, `tools/board/themes/transit.py`
- Modify: `tests/test_render.py`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_render.py`:

```python
def test_scenery_only_draws_in_large_lanes():
    # At 3-4 lanes there is no room and it would fight the text.
    from tools.board import layout
    assert layout.size_class(4) == "compact"
    b4 = Board([Watch("20", "x", "bus", [Departure(240)])] * 4, "17:42")
    assert render.render(b4).size == (320, 240)


def test_scenery_changes_a_two_lane_render():
    from tools.board import themes
    th = themes.get("transit")
    plain = themes.Theme(**{**th.__dict__, "scenery": None})
    b = scenes.SCENES["two_up"]
    assert render.render(b, theme=th).tobytes() != render.render(b, theme=plain).tobytes()


def test_scenery_is_deterministic():
    b = scenes.SCENES["two_up"]
    assert render.render(b, t=0.0).tobytes() == render.render(b, t=0.0).tobytes()
```

- [ ] **Step 2: Write the scenery module**

Create `tools/board/scenery.py`:

```python
"""Per-theme backgrounds.

A vehicle alone in a 111px lane looks abandoned. Fifteen lines of scenery is
the difference between a sprite on a rectangle and a board you want on a wall.

Every function takes a seeded Random so a lane's stars and buildings stay put
between frames - scenery must never shimmer.
"""

import math
import random


def transit(d, rect, kind, seed, th):
    """Low city skyline for the road; overhead wires for the rail."""
    x0, y0, x1, y1 = rect
    rnd = random.Random(seed)
    base = y1 - 14
    if kind == "bus":
        x = x0 + 6
        while x < x1 - 6:
            bw, bh = rnd.randint(9, 20), rnd.randint(6, 18)
            d.rectangle((x, base - bh, x + bw, base), fill=(26, 34, 48))
            for wy in range(base - bh + 3, base - 2, 5):
                for wx in range(x + 2, x + bw - 2, 5):
                    if rnd.random() < 0.35:
                        d.point((wx, wy), fill=(58, 74, 96))
            x += bw + rnd.randint(2, 6)
    else:
        d.line((x0 + 6, y0 + 30, x1 - 6, y0 + 30), fill=(34, 42, 56))
        for mx in range(x0 + 20, x1 - 10, 46):
            d.line((mx, y0 + 30, mx, base), fill=(34, 42, 56))


def ghibli(d, rect, kind, seed, th):
    """Stars over a hill for the road; stars over water for the rail."""
    x0, y0, x1, y1 = rect
    rnd = random.Random(seed)
    for _ in range(26):
        d.point((rnd.randint(x0 + 6, x1 - 6), rnd.randint(y0 + 26, y1 - 34)),
                fill=rnd.choice([(90, 84, 130), (130, 120, 170), (180, 170, 210)]))
    base = y1 - 14
    if kind == "bus":
        for x in range(x0 + 6, x1 - 6):
            h = int(7 + 5 * math.sin((x - x0) / 26.0) + 3 * math.sin((x - x0) / 9.0))
            d.line((x, base - h, x, base), fill=(30, 26, 54))
    else:
        for x in range(x0 + 6, x1 - 6):
            h = int(4 + 3 * math.sin((x - x0) / 18.0))
            d.line((x, base - h, x, base), fill=(26, 28, 60))
        for k in range(6):                     # moon path on the water
            d.line((x0 + 40 + k * 30, base - 2, x0 + 52 + k * 30, base - 2),
                   fill=(48, 52, 96))
```

- [ ] **Step 3: Wire it in**

In `tools/board/themes/transit.py`, add to the import and the `Theme(...)`:

```python
from .. import scenery
...
    scenery=scenery.transit,
```

In `tools/board/render.py`, inside `_lane`, immediately after the card
rounded-rectangle is drawn and **before** the badge:

```python
    if size == "large" and th.scenery is not None:
        th.scenery(d, (ln.rect.x0 + 8, ln.rect.y0, ln.rect.x1 - 8, ln.rect.y1),
                   watch.kind, 7 + index, th)
```

Order matters: scenery is a background, so everything else draws over it.

- [ ] **Step 4: Run and look**

Run: `python -m pytest tests/test_render.py -v`
Then regenerate and inspect:

```bash
python tools/regolden.py
```

Open `tests/golden/two_up.png`. The skyline must sit behind the bus and
below the headsign text, and the "then 17" must stay readable over it. If the
skyline collides with text, lower `base` or reduce building heights.

- [ ] **Step 5: Run everything and commit**

```bash
python -m pytest
git add tools/board/scenery.py tools/board/render.py \
        tools/board/themes/transit.py tests/test_render.py tests/golden
git commit -m "Add per-theme scenery for large lanes

Fifteen lines per theme, drawn behind everything, only at 1-2 lanes where
there is room. This is the single biggest visual improvement in the theme
work: a vehicle alone in a 111px lane looks abandoned.

Seeded per lane so stars and buildings never shimmer between frames."
```

---

### Task 5: Ghibli Night

**Files:**
- Create: `tools/board/themes/ghibli.py`
- Modify: `tools/board/themes/__init__.py`

A many-legged grinning cat-bus with slit pupils and a nose lantern, and a
lantern-lit night train carrying one tall quiet passenger in the second
carriage. Original designs, deliberately not reproductions of Studio Ghibli's
characters, because this repo is meant to be forked.

- [ ] **Step 1: Write the theme**

The art already exists in the generated data. Create
`tools/board/themes/ghibli.py`:

```python
"""Ghibli-flavoured night: warm lanterns on deep indigo.

A cat-shaped bus with too many legs, and a night train crossing water with a
tall quiet passenger aboard. Original art, not reproductions.
"""

from .. import scenery
from ..art import load
from .base import Theme

COLOURS = {
    "bg":       (14, 12, 34),
    "panel":    (26, 22, 50),
    "panel_hi": (36, 31, 64),
    "line":     (54, 46, 86),
    "text":     (250, 240, 220),
    "dim":      (168, 156, 190),
    "live":     (150, 210, 140),
    "warn":     (255, 190, 90),
    "road":     (44, 36, 60),
    "rail":     (52, 40, 52),
    "window":   (255, 248, 226),
    "dark":     (24, 18, 34),
}

THEME = Theme(
    name="ghibli",
    label="Ghibli Night",
    colours=COLOURS,
    sprites=load("ghibli"),
    kind_fallback={"bus": (232, 168, 74), "train": (150, 178, 226)},
    roles={"H": "bright", "S": "shade", "M": "shade",
           "W": (255, 248, 226), "G": (255, 255, 255),
           "D": (28, 18, 30), "K": (36, 22, 34),
           "L": (255, 210, 120), "A": (196, 132, 60)},
    use_route_color=False,
    scenery=scenery.ghibli,
)
```

- [ ] **Step 2: Register it**

```python
from . import ghibli, transit

_ALL = [transit.THEME, ghibli.THEME]
```

- [ ] **Step 3: Run the suite — it covers ghibli automatically now**

Run: `python -m pytest tests/test_themes.py -v`
Expected: PASS, roughly double the count.

The likely failure is `test_badge_ink_is_legible_on_every_badge_colour` for
the train's periwinkle. If it fails, lighten `kind_fallback["train"]`. Never
lower the threshold.

- [ ] **Step 4: Look at it**

```bash
python -c "import sys;sys.path.insert(0,'.');from tools.board import render,scenes,themes;render.render(scenes.SCENES['two_up'],theme=themes.get('ghibli')).resize((960,720),0).save('tools/out/ghibli.png')"
```

The cat-bus's eyes must read against its amber body, the passenger must be
visible in the second carriage, and the stars must not sit on top of the text.

- [ ] **Step 5: Commit**

```bash
git add tools/board/themes/ghibli.py tools/board/themes/__init__.py
git commit -m "Add Ghibli Night theme"
```

---

### Task 6: `--theme` in the simulator, and themed goldens

**Files:**
- Modify: `tools/simulate.py`, `tools/regolden.py`, `tests/test_render.py`, `tests/test_simulate.py`

- [ ] **Step 1: Add `--theme`**

In `tools/simulate.py`, add `from tools.board import themes` and:

```python
def contact_sheet(names, scale=2, theme=None):
    th = themes.get(theme) if theme else None
    imgs = [(n, render.render(scenes.SCENES[n], theme=th)) for n in names]
    ...


def animate(board, frames=48, fps=12, theme=None):
    th = themes.get(theme) if theme else None
    return [render.render(board, t=i / fps, theme=th) for i in range(frames)]
```

Add the argument, validate it, and suffix output filenames so themes do not
overwrite each other:

```python
    ap.add_argument("--theme", default=None)
    ...
    if args.theme and args.theme not in themes.names():
        raise SystemExit(
            f"unknown theme {args.theme!r}. known: {', '.join(themes.names())}")
    suffix = f"_{args.theme}" if args.theme else ""
    sheet_path = os.path.join(OUT, f"scenes{suffix}.png")
    ...
    p = os.path.join(OUT, f"{n}{suffix}.gif")
```

- [ ] **Step 2: Extend regolden**

In `tools/regolden.py`, add `themes` to the imports and append to `main()`:

```python
# All scenes for the default theme; two representative scenes for the rest.
# A golden nobody looked at is worthless, so this stays reviewable by hand.
EXTRA_SCENES = ["two_up", "empty"]

    for theme in themes.names():
        if theme == themes.DEFAULT:
            continue
        for scene in EXTRA_SCENES:
            path = os.path.join(GOLDEN, f"{theme}__{scene}.png")
            render.render(scenes.SCENES[scene], t=0.0,
                          theme=themes.get(theme)).save(path)
            print("wrote", os.path.normpath(path))
```

- [ ] **Step 3: Add the tests**

Append to `tests/test_render.py`:

```python
from tools.board import themes

THEMED = [(th, sc) for th in themes.names() if th != themes.DEFAULT
          for sc in ("two_up", "empty")]


@pytest.mark.parametrize("theme,scene", THEMED)
def test_themed_render_matches_golden(theme, scene):
    path = os.path.join(GOLDEN, f"{theme}__{scene}.png")
    assert os.path.exists(path), (
        f"no golden for {theme}/{scene}. Run `python tools/regolden.py`.")
    expected = Image.open(path).convert("RGB")
    actual = render.render(scenes.SCENES[scene], t=0.0, theme=themes.get(theme))
    assert actual.tobytes() == expected.tobytes(), (
        f"render of {theme}/{scene} changed. If intended, regenerate goldens.")


@pytest.mark.parametrize("theme", themes.names())
@pytest.mark.parametrize("n", [1, 2, 3, 4])
def test_every_theme_renders_at_every_lane_count(theme, n):
    from tools.board.model import Board, Departure, Watch
    b = Board([Watch("20", "to town", "bus" if i % 2 == 0 else "train",
                     [Departure(240 * (i + 1))]) for i in range(n)], "17:42")
    assert render.render(b, theme=themes.get(theme)).size == (320, 240)
```

Append to `tests/test_simulate.py`:

```python
from tools.board import themes


def test_contact_sheet_accepts_a_theme():
    a = simulate.contact_sheet(["two_up"], scale=1)
    b = simulate.contact_sheet(["two_up"], scale=1, theme="ghibli")
    assert a.tobytes() != b.tobytes()


def test_unknown_theme_is_rejected():
    with pytest.raises(KeyError):
        simulate.contact_sheet(["two_up"], scale=1, theme="nope")
```

- [ ] **Step 4: Generate, inspect, run**

```bash
python tools/regolden.py
python tools/simulate.py --all --theme ghibli
python -m pytest -v
```

Open `tools/out/scenes_ghibli.png` and check every scene — especially `empty`
(1am, nothing left) and `cancelled`.

- [ ] **Step 5: Commit**

```bash
git add tools/simulate.py tools/regolden.py tests/golden \
        tests/test_render.py tests/test_simulate.py
git commit -m "Add --theme to the simulator and freeze themed goldens"
```

---

### Task 7: Export both size classes of both themes to C

**Files:**
- Modify: `tools/export_sprites.py`, `tests/test_export.py`, `README.md`
- Regenerate: `src/sprites.h`

- [ ] **Step 1: Update the exporter**

In `tools/export_sprites.py`, extend `ROLE_INDEX` and `ROLE_CONST` to all 11
roles, and rewrite `render_header()`:

```python
ROLE_INDEX = {".": 0, "B": 1, "H": 2, "S": 3, "M": 4, "W": 5,
              "G": 6, "D": 7, "K": 8, "L": 9, "A": 10}

ROLE_CONST = {".": "ROLE_NONE", "B": "ROLE_BODY", "H": "ROLE_HIGHLIGHT",
              "S": "ROLE_SHADE", "M": "ROLE_MIDSHADOW", "W": "ROLE_WINDOW",
              "G": "ROLE_GLINT", "D": "ROLE_OUTLINE", "K": "ROLE_DETAIL",
              "L": "ROLE_LAMP", "A": "ROLE_ACCENT"}


def render_header():
    parts = [
        "// generated by tools/export_sprites.py - do not edit by hand",
        "// regenerate after running tools/author_art.py",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "// 4bpp indexed. Two pixels per byte, high nibble leftmost.",
        "// Index 0 is transparent; the rest are roles the UI maps to colours,",
        "// which is what lets one sprite render in any route's colour.",
    ]
    for letter, const in ROLE_CONST.items():
        parts.append(f"#define {const} {ROLE_INDEX[letter]}")

    names = themes.names()
    parts += ["", f"#define THEME_COUNT {len(names)}"]
    for i, n in enumerate(names):
        parts.append(f"#define THEME_{n.upper()} {i}")

    for n in names:
        th = themes.get(n)
        for size in ("large", "compact"):
            for kind in ("bus", "train"):
                s = th.sprite(size, kind)
                tag = f"{n.upper()}_{size.upper()}_{kind.upper()}"
                parts += [
                    "",
                    f"#define SPRITE_{tag}_W {s.width}",
                    f"#define SPRITE_{tag}_H {s.height}",
                    f"#define SPRITE_{tag}_STRIDE {(s.width + 1) // 2}",
                    _array(f"SPRITE_{tag}_DATA", pack(s)),
                ]
    return "\n".join(parts) + "\n"
```

Add `from tools.board import themes` to its imports.

- [ ] **Step 2: Update the export tests**

Replace the sprite-specific tests in `tests/test_export.py`:

```python
from tools.board import themes

ALL_THEMES = themes.names()


@pytest.mark.parametrize("name", ALL_THEMES)
def test_pack_round_trips_back_to_the_original_grid(name):
    rev = {v: k for k, v in ex.ROLE_INDEX.items()}
    for (size, kind), s in themes.get(name).sprites.items():
        data = ex.pack(s)
        stride = (s.width + 1) // 2
        assert len(data) == stride * s.height
        for y, row in enumerate(s.rows):
            for x, ch in enumerate(row):
                byte = data[y * stride + x // 2]
                nib = (byte >> 4) if x % 2 == 0 else (byte & 0x0F)
                assert rev[nib] == ch, f"{name}/{size}/{kind} at ({x},{y})"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_header_declares_every_theme_and_size(name):
    h = ex.render_header()
    assert f"THEME_{name.upper()}" in h
    for size in ("LARGE", "COMPACT"):
        for kind in ("BUS", "TRAIN"):
            assert f"SPRITE_{name.upper()}_{size}_{kind}_DATA" in h


def test_all_eleven_roles_are_exported():
    h = ex.render_header()
    for const in ex.ROLE_CONST.values():
        assert const in h


def test_indices_fit_in_a_nibble():
    assert all(0 <= v <= 15 for v in ex.ROLE_INDEX.values())
```

Keep `test_header_has_an_include_guard` and
`test_header_warns_against_hand_editing`.

- [ ] **Step 3: Generate and check the size**

```bash
python tools/export_sprites.py
grep -c "_DATA\[" src/sprites.h     # expect 8
grep "THEME_COUNT" src/sprites.h    # expect 2
```

Large art is bigger: 76x40 at 4bpp is 1520 bytes. All eight together are
roughly 5 KB of flash — still irrelevant against 4 MB.

- [ ] **Step 4: Update the README**

In `README.md`, add to the Layout table:

```markdown
| Themes (palette, sprites, scenery) | `tools/board/themes/` |
| Vehicle art (parametric source) | `tools/author_art.py` |
```

and under "Try it without hardware":

````markdown
Two themes ship: `transit` and `ghibli`. Sprites come in two sizes — large art
at 1–2 watches, compact at 3–4, because a 40px vehicle does not fit a 55px lane.

```bash
python tools/simulate.py --scene two_up --theme ghibli
python tools/author_art.py --preview     # redraw the vehicles
```
````

- [ ] **Step 5: Run everything and commit**

```bash
python -m pytest
git add tools/export_sprites.py src/sprites.h tests/test_export.py README.md
git commit -m "Export both size classes of both themes to C

Eight sprites, eleven roles, about 5KB of flash. Per-sprite dimensions
replace the single SPRITE_H now that sizes differ."
```

---

## Self-Review

**Spec coverage.** Theme controls palette + sprites + scenery (Tasks 1, 4).
Per-theme role meanings (Task 1). `use_route_color`, transit only (Task 1).
Two size classes (Task 3). Parametrised cross-theme tests including contrast
(Task 2). Reduced golden coverage (Task 6). C export (Task 7).

**Placeholder scan.** No TBDs. No pixel art is transcribed anywhere — it is
loaded from `tools/board/art/sprites_data.py`, which already exists and is
verified. That was the single largest error source in the previous draft.

**Type consistency.** `Theme.sprite(size, kind)` used identically in Tasks 1,
3, 5, 7. `sprites.blit(draw, sprite, x, y, colours)` consistent in Tasks 1, 3.
`layout.size_class(n)` defined Task 3, used in Tasks 3, 4, 6.
`scenery.<fn>(d, rect, kind, seed, th)` defined Task 4, referenced in Tasks 4,
5. `render.render(board, t, theme)` consistent throughout.

**Known risks.**
1. Task 1 is the risky one — a refactor whose success criterion is that
   nothing changes. Unregenerated goldens are the check.
2. Task 3 and Task 4 both regenerate goldens **legitimately**. Do not confuse
   this with Task 1, where a golden change means a bug.
3. Ghibli's periwinkle train badge may fail the contrast check. Fix the
   colour, not the threshold.
4. Scenery could collide with the headsign or "then N" text at some lane
   heights. Step 4 of Task 4 says to look; take that seriously.
