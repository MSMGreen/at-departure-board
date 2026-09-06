# Theme System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the board's whole look selectable — palette and vehicle sprites
together — across six themes, with every theme automatically inheriting the
full test suite.

**Architecture:** A `Theme` is data: a colour dict, a sprite pair, a badge
fallback palette, and a mapping saying what each sprite *role* means. Today's
look becomes `themes/transit.py` and must render byte-identically, so the
existing goldens are the regression proof that the refactor changed nothing.
New themes are then pure data files.

**Tech Stack:** Python 3.10, Pillow 9.4, pytest. No firmware in this plan.

**Spec:** `docs/superpowers/specs/2026-09-06-at-departure-board-design.md` §7,
extended by the in-chat design agreed 2026-09-06. All sprite art below was
authored on a char grid, validated, and visually reviewed before this plan was
written — it is not a sketch.

## Global Constraints

- Screen 320x240. Status bar 18px. Lanes `(240-18)//n`, n in 1..4.
- **Sprite height cap is 18px.** At n=4 the lane is 55px, badge occupies to
  `y0+24`, track sits at `y1-12` = `y0+43`. 43-24 = 19, so 18 is the last safe
  height. A test enforces this.
- New-theme sprites are all **16 rows**; transit's stay 14. Bus 34-36 wide,
  train 48.
- Roles are still `. B S W D L`. What each role *means* is per-theme.
- **Only `transit` honours AT's `route_color`.** Every stylised theme uses its
  own palette — AT's `#97C93D` green in the neon palette would read as a bug.
- Golden coverage: all 8 scenes for `transit`; `two_up` and `empty` only for
  each other theme. 18 goldens total, each one actually looked at.
- `paper` is a **light** theme. It exists partly to catch anywhere the code
  assumes a dark ground.

---

## File Structure

```
tools/board/themes/__init__.py    registry: THEMES, get(), DEFAULT, names()
tools/board/themes/base.py        Theme dataclass + role resolution
tools/board/themes/transit.py     today's look, moved verbatim
tools/board/themes/ghibli.py
tools/board/themes/arcade.py
tools/board/themes/blueprint.py
tools/board/themes/paper.py
tools/board/themes/neon.py
tests/test_themes.py              cross-theme suite, parametrised over all
```

Modified: `palette.py` (keeps only shared helpers), `sprites.py` (keeps
`Sprite` + `blit`), `render.py` (takes a theme), `layout.py` (`vehicle_x` takes
a width), `model.py` (`Board.theme`), `simulate.py` (`--theme`),
`export_sprites.py` (emits all themes), `regolden.py`, `tests/test_*.py`.

---

### Task 1: Theme type, registry, and the transit extraction

The refactor task. Nothing should look different when it lands — that is the
point, and the existing goldens prove it.

**Files:**
- Create: `tools/board/themes/__init__.py`, `themes/base.py`, `themes/transit.py`
- Modify: `tools/board/palette.py`, `sprites.py`, `layout.py`, `render.py`, `model.py`
- Modify: `tests/test_palette.py`, `tests/test_layout.py`, `tests/test_sprites.py`

**Interfaces:**
- Produces:
  - `themes.base.Theme(name, label, colours, sprites, kind_fallback, roles, use_route_color)`
  - `Theme.colour(key) -> tuple` — raises `KeyError` naming the theme if missing
  - `Theme.badge_colour(kind, route_color=None) -> tuple`
  - `Theme.role_colours(body) -> dict[str, tuple]` — resolves `"shade"`/`"bright"`
  - `themes.get(name) -> Theme`, `themes.names() -> list[str]`, `themes.DEFAULT = "transit"`
  - `palette.ROLES`, `palette.parse_hex`, `palette.shade`, `palette.bright` (kept)
  - `sprites.Sprite`, `sprites.blit(draw, sprite, x, y, role_colours)` — **signature
    changes**: takes a `Sprite` object and a resolved colour dict, not a name and a body colour
  - `layout.vehicle_x(eta_s, lane, sprite_width)` — **signature changes**: width, not kind
  - `render.render(board, t=0.0, theme=None)` — `None` means look up `board.theme`
  - `model.Board.theme: str = "transit"`

- [ ] **Step 1: Write the failing theme-type test**

Create `tests/test_themes.py`:

```python
import pytest

from tools.board import themes


def test_default_theme_exists():
    assert themes.DEFAULT in themes.names()


def test_transit_is_the_default():
    assert themes.DEFAULT == "transit"


def test_get_returns_a_theme_with_its_own_name():
    t = themes.get("transit")
    assert t.name == "transit"


def test_unknown_theme_names_what_is_available():
    with pytest.raises(KeyError) as e:
        themes.get("nope")
    assert "transit" in str(e.value)


def test_missing_colour_key_names_the_theme():
    t = themes.get("transit")
    with pytest.raises(KeyError) as e:
        t.colour("no_such_colour")
    assert "transit" in str(e.value)


def test_shade_and_bright_resolve_against_the_body_colour():
    t = themes.get("transit")
    cols = t.role_colours((100, 150, 200))
    assert cols["B"] == (100, 150, 200)
    assert cols["S"] != cols["B"]


def test_transit_honours_at_route_colour():
    assert themes.get("transit").badge_colour("train", "97C93D") == (151, 201, 61)
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `python -m pytest tests/test_themes.py -v`
Expected: FAIL, `ModuleNotFoundError: No module named 'tools.board.themes'`

- [ ] **Step 3: Write the Theme type**

Create `tools/board/themes/base.py`:

```python
"""A theme is data: colours, sprites, and what each sprite role means."""

from dataclasses import dataclass
from typing import Dict, Optional, Tuple

from .. import palette
from ..sprites import Sprite


@dataclass(frozen=True)
class Theme:
    name: str
    label: str
    colours: Dict[str, tuple]
    sprites: Dict[str, Sprite]
    kind_fallback: Dict[str, tuple]
    roles: Dict[str, object]      # tuple, or "shade" / "bright" / "body"
    use_route_color: bool = False

    def colour(self, key) -> Tuple[int, int, int]:
        try:
            return self.colours[key]
        except KeyError:
            raise KeyError(f"theme {self.name!r} has no colour {key!r}") from None

    def badge_colour(self, kind, route_color: Optional[str] = None):
        if self.use_route_color:
            parsed = palette.parse_hex(route_color)
            if parsed:
                return parsed
        return self.kind_fallback.get(kind, self.colour("dim"))

    def role_colours(self, body):
        """Resolve every role to concrete RGB for one vehicle colour."""
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

    def sprite(self, kind) -> Sprite:
        return self.sprites[kind]
```

- [ ] **Step 4: Write the registry**

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

- [ ] **Step 5: Move transit's data into its own theme**

Create `tools/board/themes/transit.py`. Copy `BUS` and `TRAIN` **verbatim** from
`tools/board/sprites.py` and `BASE`/`KIND_FALLBACK` **verbatim** from
`tools/board/palette.py`. Do not retype them — a byte difference breaks the
goldens, which is exactly the check we want to keep meaningful.

```python
"""The default look: a transit board. The only theme that honours AT's own
route colours, because it is the only one pretending to be signage."""

from ..sprites import Sprite
from .base import Theme

BUS = Sprite(34, 14, [
    # ... copied verbatim from sprites.py
])

TRAIN = Sprite(48, 14, [
    # ... copied verbatim from sprites.py
])

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
}

THEME = Theme(
    name="transit",
    label="Transit",
    colours=COLOURS,
    sprites={"bus": BUS, "train": TRAIN},
    kind_fallback={"bus": (0, 168, 224), "train": (151, 201, 61)},
    roles={"S": "shade", "W": (210, 240, 255), "D": (20, 24, 30),
           "L": (245, 176, 66)},
    use_route_color=True,
)
```

- [ ] **Step 6: Reduce `palette.py` to shared helpers**

Delete `BASE`, `KIND_FALLBACK`, `kind_colour` and `resolve_badge_colour` from
`tools/board/palette.py` — they now live per theme. Keep `ROLES`, `parse_hex`,
`shade`, and add `bright`:

```python
def bright(rgb, factor=1.6, floor=40):
    """Brighten toward a glow. Used by neon, where the halo is the body colour."""
    return tuple(min(255, int(c * factor) + floor) for c in rgb)
```

Delete `tests/test_palette.py` tests that referenced `BASE`/`kind_colour`, and
keep the `parse_hex` behaviours by rewriting them against `parse_hex` directly:

```python
def test_leading_hash_is_accepted():
    assert palette.parse_hex("#97C93D") == (151, 201, 61)


def test_black_is_treated_as_absent_not_painted():
    assert palette.parse_hex("000000") is None


def test_malformed_colour_is_none():
    assert palette.parse_hex("nonsense") is None


def test_empty_is_none():
    assert palette.parse_hex(None) is None


def test_bright_moves_away_from_black():
    assert palette.bright((10, 10, 10)) > (10, 10, 10)
```

- [ ] **Step 7: Change `sprites.blit` to take a sprite and resolved colours**

In `tools/board/sprites.py`, delete `BUS`, `TRAIN`, `SPRITES` and
`role_colours` (all now per-theme). Keep `Sprite`, and change `blit`:

```python
def blit(draw, sprite, x, y, colours):
    """Draw `sprite` with its BOTTOM-left corner at (x, y).

    `colours` is a resolved role->RGB dict from Theme.role_colours().
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

- [ ] **Step 8: Change `layout.vehicle_x` to take a width**

In `tools/board/layout.py`, remove `from . import sprites` and change:

```python
def vehicle_x(eta_s, lane, sprite_width):
    """Map seconds-until-arrival to an x position on the lane's track.

    HORIZON_S or more -> the left end. Zero or less -> touching the marker.
    """
    x_start = lane.track.x0
    x_stop = lane.marker_x - sprite_width
    clamped = max(0, min(int(eta_s), HORIZON_S))
    progress = 1.0 - clamped / HORIZON_S
    return int(round(x_start + progress * (x_stop - x_start)))
```

This removes `layout`'s dependency on a global sprite table, which is what
allows themes to have differently sized vehicles.

Update `tests/test_layout.py`: every `layout.vehicle_x(s, ln, "bus")` becomes
`layout.vehicle_x(s, ln, 34)` and `"train"` becomes `48`. In
`test_the_widest_sprite_always_fits_the_track`, replace the `sprites` import
with a literal `48`. In `test_a_train_is_wider_so_it_stops_further_left_than_a_bus`,
use widths `48` and `34`.

Update `tests/test_sprites.py`: it referenced `sprites.SPRITES`. Delete it —
Task 2 replaces it with the cross-theme suite that covers every theme's art.

- [ ] **Step 9: Thread the theme through `model` and `render`**

In `tools/board/model.py`, add to `Board`:

```python
    theme: str = "transit"
```

In `tools/board/render.py`, replace the `palette`/`sprites` imports and every
`palette.BASE[key]` with `th.colour(key)`. The function signature becomes:

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

`_status_bar` and `_lane` take `th` as their last parameter. Inside `_lane`:

- `colour = th.badge_colour(watch.kind, watch.route_color)` replaces `watch.colour`
- `sprite = th.sprite(watch.kind)`
- `x = layout.vehicle_x(nxt.eta_s, ln, sprite.width)`
- `sprites.blit(d, sprite, x, ln.sprite_baseline + bob, th.role_colours(body))`
- the parked case: `th.role_colours(palette.shade(colour, 0.4))`
- badge ink `fill=th.colour("dark")` — **add `"dark"` and `"window"` to every
  theme's `colours` dict**, since `_lane` reads them for badge text.

In `tools/board/model.py`, `Watch.colour` now has no palette to consult. Delete
the property and let `render` ask the theme. Update `tests/test_model.py`:
delete `test_a_bus_takes_the_fallback_colour` and
`test_rail_takes_ats_colour_when_present`; the equivalent coverage lives in
`tests/test_themes.py`.

- [ ] **Step 10: Run everything — goldens must still pass unchanged**

Run: `python -m pytest -v`
Expected: PASS. **The 8 transit goldens must pass without regeneration.** If any
golden fails, the refactor changed rendering — find the difference rather than
regenerating. That is the entire safety value of this task.

- [ ] **Step 11: Commit**

```bash
git add tools/board/themes tools/board/palette.py tools/board/sprites.py \
        tools/board/layout.py tools/board/render.py tools/board/model.py \
        tests/test_themes.py tests/test_palette.py tests/test_layout.py \
        tests/test_model.py
git rm tests/test_sprites.py
git commit -m "Extract the current look into a transit theme

Pure refactor: a Theme is colours + sprites + what each role means. The
eight existing goldens pass unregenerated, which is the proof that nothing
about rendering changed.

layout.vehicle_x now takes a width instead of a kind, dropping its
dependency on a global sprite table - that is what lets themes carry
differently sized vehicles."
```

---

### Task 2: The cross-theme test suite

Written before the new themes so each one is validated the moment it arrives.

**Files:**
- Modify: `tests/test_themes.py`

**Interfaces:**
- Consumes: `themes.all_themes()`.
- Produces: the parametrised suite every future theme inherits automatically.

- [ ] **Step 1: Add the suite**

Append to `tests/test_themes.py`:

```python
from tools.board import palette

REQUIRED_COLOURS = ["bg", "panel", "panel_hi", "line", "text", "dim",
                    "live", "warn", "road", "rail", "window", "dark"]

ALL_THEMES = [t.name for t in themes.all_themes()]
HEIGHT_CAP = 18


def _lum(rgb):
    """WCAG relative luminance."""
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
    missing = [k for k in REQUIRED_COLOURS if k not in t.colours]
    assert missing == [], f"{name} missing colours: {missing}"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_has_both_vehicle_kinds(name):
    assert set(themes.get(name).sprites) >= {"bus", "train"}


@pytest.mark.parametrize("name", ALL_THEMES)
def test_has_a_badge_fallback_for_both_kinds(name):
    assert set(themes.get(name).kind_fallback) >= {"bus", "train"}


@pytest.mark.parametrize("name", ALL_THEMES)
def test_sprite_rows_match_declared_width(name):
    for kind, s in themes.get(name).sprites.items():
        bad = [(i, len(r)) for i, r in enumerate(s.rows) if len(r) != s.width]
        assert bad == [], f"{name}/{kind} rows with wrong width: {bad}"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_sprite_row_count_matches_height(name):
    for kind, s in themes.get(name).sprites.items():
        assert len(s.rows) == s.height, f"{name}/{kind}"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_sprites_fit_the_four_lane_layout(name):
    # At n=4 the lane is 55px; badge ends at y0+24 and the track sits at
    # y0+43. Anything taller than 18 collides with the badge.
    for kind, s in themes.get(name).sprites.items():
        assert s.height <= HEIGHT_CAP, f"{name}/{kind} is {s.height}px"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_only_known_roles_are_used(name):
    for kind, s in themes.get(name).sprites.items():
        used = {c for row in s.rows for c in row}
        assert used <= set(palette.ROLES), f"{name}/{kind}: {used - set(palette.ROLES)}"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_there_is_a_light_at_the_leading_edge(name):
    # Direction of travel must read. This asserts a light EXISTS at the front,
    # not that lights appear only there - the ghibli train is lantern-lit
    # along its whole length by design.
    for kind, s in themes.get(name).sprites.items():
        xs = [x for row in s.rows for x, c in enumerate(row) if c == "L"]
        assert xs, f"{name}/{kind} has no light at all"
        assert max(xs) >= s.width - 3, f"{name}/{kind} light is not at the front"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_every_role_used_in_art_resolves_to_a_colour(name):
    t = themes.get(name)
    cols = t.role_colours((120, 120, 120))
    for kind, s in t.sprites.items():
        used = {c for row in s.rows for c in row} - {"."}
        missing = used - set(cols)
        assert not missing, f"{name}/{kind} uses unresolvable roles: {missing}"


# --- legibility ----------------------------------------------------------
# A theme that looks lovely in a contact sheet and cannot be read from the
# hallway is a failed theme. If one of these fails, change the theme's
# colours - never the threshold.

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
        assert contrast(t.colour("dark"), fill) >= 3.0, f"{name}/{kind} badge"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_panel_is_distinguishable_from_the_background(name):
    t = themes.get(name)
    assert contrast(t.colour("panel"), t.colour("bg")) >= 1.05


@pytest.mark.parametrize("name", ALL_THEMES)
def test_alternating_lanes_are_distinguishable(name):
    t = themes.get(name)
    assert contrast(t.colour("panel"), t.colour("panel_hi")) >= 1.05
```

- [ ] **Step 2: Run it**

Run: `python -m pytest tests/test_themes.py -v`
Expected: PASS for `transit`. If a contrast test fails on transit, adjust
`transit.COLOURS` and regenerate the goldens in the same commit.

- [ ] **Step 3: Commit**

```bash
git add tests/test_themes.py
git commit -m "Add cross-theme validation suite

Parametrised over every registered theme, so a new theme inherits the whole
suite by existing. Includes WCAG contrast checks: a theme that is beautiful
in a contact sheet and unreadable from the hallway is a failed theme.

The leading-edge light test asserts a light EXISTS at the front rather than
that lights appear only there - the original version over-constrained and
would reject a lantern-lit train."
```

---

### Task 3: Ghibli Night

**Files:**
- Create: `tools/board/themes/ghibli.py`
- Modify: `tools/board/themes/__init__.py`

A many-legged grinning cat-bus with pale eyes and a nose lantern, and a
lantern-lit night train carrying one tall shadowy passenger, visible in the
second carriage. Original designs — deliberately *not* reproductions of Studio
Ghibli's characters, because this repo is meant to be forked.

- [ ] **Step 1: Write the theme**

Create `tools/board/themes/ghibli.py`:

```python
"""Ghibli-flavoured night: warm lanterns on deep indigo.

A cat-shaped bus with too many legs, and a night train crossing water with a
tall quiet passenger aboard. Original art, not reproductions.
"""

from ..sprites import Sprite
from .base import Theme

BUS = Sprite(36, 16, [
    "....................................",
    "........................BBB...BBB...",
    "........................BBB...BBB...",
    "...BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB...",
    "..BBBBBBBBBBBBBBBBBBBBBBBBBWWWBWWW..",
    ".BBBWWWWBBWWWWBBWWWWBBBBBBBWDWBWDWB.",
    ".BBBWWWWBBWWWWBBWWWWBBBBBBBWWWBWWWB.",
    ".BBBWWWWBBWWWWBBWWWWBBBBBBBBBBBBBBB.",
    ".BBBBBBBBBBBBBBBBBBBBBBBBBDDDDDDDDLL",
    ".BBBBBBBBBBBBBBBBBBBBBBBBBBBDBBDBBLL",
    ".SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS.",
    ".SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS.",
    "...DD...DD...DD...DD...DD...DD......",
    "...DD...DD...DD...DD...DD...DD......",
    "...DD...DD...DD...DD...DD...DD......",
    "....................................",
])

TRAIN = Sprite(48, 16, [
    "................................................",
    "................................................",
    "................................................",
    ".BBBBBBBBBBBBBBBBBBBB.....BBBBBBBBBBBBBBBBBBBB..",
    "BBBBBBBBBBBBBBBBBBBBBB...BBBBBBDWWDBBBBBBBBBBBB.",
    "BBBLLLLBBLLLLBBLLLLBBB...BBBLLLDDDDLLLBBLLLLBBB.",
    "BBBLLLLBBLLLLBBLLLLBBB...BBBLLLDDDDLLLBBLLLLBBLL",
    "BBBLLLLBBLLLLBBLLLLBBB...BBBLLLDDDDLLLBBLLLLBBLL",
    "BBBBBBBBBBBBBBBBBBBBBB...BBBBBBDDDDBBBBBBBBBBBB.",
    "BBBBBBBBBBBBBBBBBBBBBB...BBBBBBBBBBBBBBBBBBBBBB.",
    ".BBBBBBBBBBBBBBBBBBBB.....BBBBBBBBBBBBBBBBBBBB..",
    ".SSSSSSSSSSSSSSSSSSSS.....SSSSSSSSSSSSSSSSSSSS..",
    "...SSS.........SSS..........SSS.........SSS.....",
    "................................................",
    "........SS............SS...........SS...........",
    "................................................",
])

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
    "window":   (255, 244, 214),
    "dark":     (24, 18, 34),
}

THEME = Theme(
    name="ghibli",
    label="Ghibli Night",
    colours=COLOURS,
    sprites={"bus": BUS, "train": TRAIN},
    kind_fallback={"bus": (232, 168, 74), "train": (150, 178, 226)},
    roles={"S": "shade", "W": (255, 244, 214), "D": (24, 18, 34),
           "L": (255, 205, 110)},
    use_route_color=False,
)
```

- [ ] **Step 2: Register it**

In `tools/board/themes/__init__.py`:

```python
from . import ghibli, transit

_ALL = [transit.THEME, ghibli.THEME]
```

- [ ] **Step 3: Run the suite — it now covers ghibli automatically**

Run: `python -m pytest tests/test_themes.py -v`
Expected: PASS, roughly double the test count of Task 2.

If `test_badge_ink_is_legible_on_every_badge_colour` fails for the train,
lighten `kind_fallback["train"]` until it passes. Do not lower the threshold.

- [ ] **Step 4: Look at it**

```bash
python tools/simulate.py --scene two_up --theme ghibli
```

(The `--theme` flag arrives in Task 6; until then, use a throwaway one-liner:)

```bash
python -c "import sys;sys.path.insert(0,'.');from tools.board import render,scenes,themes;render.render(scenes.SCENES['two_up'],theme=themes.get('ghibli')).resize((960,720),0).save('tools/out/ghibli.png')"
```

Open `tools/out/ghibli.png`. The cat-bus's eyes must read against its body, and
the passenger must be visible in the second carriage.

- [ ] **Step 5: Commit**

```bash
git add tools/board/themes/ghibli.py tools/board/themes/__init__.py
git commit -m "Add Ghibli Night theme

A many-legged cat-bus with pale eyes and a nose lantern, and a lantern-lit
night train carrying one tall quiet passenger. Original art rather than
reproductions of Studio Ghibli's characters, because this repo is meant to
be forked.

Inherits the full cross-theme suite by existing."
```

---

### Task 4: Arcade and Blueprint

**Files:**
- Create: `tools/board/themes/arcade.py`, `tools/board/themes/blueprint.py`
- Modify: `tools/board/themes/__init__.py`

- [ ] **Step 1: Write Arcade**

Create `tools/board/themes/arcade.py`:

```python
"""8-bit arcade: hard black outlines, saturated primaries, no gradients.

320x240 is the native idiom for this look, which is why it reads so well.
"""

from ..sprites import Sprite
from .base import Theme

BUS = Sprite(36, 16, [
    "....................................",
    "....................................",
    ".DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD.",
    ".DBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBD.",
    ".DBBWWWWBBWWWWBBWWWWBBWWWWBBBBBLLLD.",
    ".DBBWWWWBBWWWWBBWWWWBBWWWWBBBBBLLLD.",
    ".DBBWWWWBBWWWWBBWWWWBBWWWWBBBBBLLLD.",
    ".DBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBD.",
    ".DBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBD.",
    ".DSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSD.",
    ".DSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSD.",
    ".DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD.",
    ".....DDDDD..............DDDDD.......",
    ".....DDDDD..............DDDDD.......",
    ".....DDDDD..............DDDDD.......",
    "....................................",
])

TRAIN = Sprite(48, 16, [
    "................................................",
    "................................................",
    "DDDDDDDDDDDDDDDDDDDDDDD..DDDDDDDDDDDDDDDDDDDDDDD",
    "DBBBBBBBBBBBBBBBBBBBBBD..DBBBBBBBBBBBBBBBBBBBBBD",
    "DBBWWWWWBBWWWWWBWWWWWBD..DBBWWWWWBBWWWWWBWWWWLLL",
    "DBBWWWWWBBWWWWWBWWWWWBD..DBBWWWWWBBWWWWWBWWWWLLL",
    "DBBWWWWWBBWWWWWBWWWWWBD..DBBWWWWWBBWWWWWBWWWWLLL",
    "DBBBBBBBBBBBBBBBBBBBBBD..DBBBBBBBBBBBBBBBBBBBBBD",
    "DBBBBBBBBBBBBBBBBBBBBBD..DBBBBBBBBBBBBBBBBBBBBBD",
    "DSSSSSSSSSSSSSSSSSSSSSD..DSSSSSSSSSSSSSSSSSSSSSD",
    "DSSSSSSSSSSSSSSSSSSSSSD..DSSSSSSSSSSSSSSSSSSSSSD",
    "DDDDDDDDDDDDDDDDDDDDDDD..DDDDDDDDDDDDDDDDDDDDDDD",
    "....DDDD.......DDDD..........DDDD.......DDDD....",
    "....DDDD.......DDDD..........DDDD.......DDDD....",
    "....DDDD.......DDDD..........DDDD.......DDDD....",
    "................................................",
])

COLOURS = {
    "bg":       (16, 16, 16),
    "panel":    (32, 30, 36),
    "panel_hi": (44, 42, 50),
    "line":     (70, 66, 78),
    "text":     (255, 255, 255),
    "dim":      (170, 166, 180),
    "live":     (60, 220, 90),
    "warn":     (255, 200, 40),
    "road":     (48, 44, 52),
    "rail":     (56, 48, 44),
    "window":   (232, 246, 255),
    "dark":     (10, 10, 12),
}

THEME = Theme(
    name="arcade",
    label="Arcade",
    colours=COLOURS,
    sprites={"bus": BUS, "train": TRAIN},
    kind_fallback={"bus": (232, 60, 60), "train": (60, 200, 90)},
    roles={"S": "shade", "W": (232, 246, 255), "D": (10, 10, 12),
           "L": (255, 232, 64)},
    use_route_color=False,
)
```

- [ ] **Step 2: Write Blueprint**

Create `tools/board/themes/blueprint.py`:

```python
"""Technical schematic: cyan hairlines on deep navy, vehicles as outlines."""

from ..sprites import Sprite
from .base import Theme

BUS = Sprite(36, 16, [
    "....................................",
    "....................................",
    "....................................",
    ".BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.",
    ".B..........................B.....B.",
    ".B...BBBB..BBBB..BBBB..BBBB.B...LLB.",
    ".B..........................B...LLB.",
    ".B..........................B.....B.",
    ".B..........................B.....B.",
    ".B..........................B.....B.",
    ".BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.",
    "......BBBB...............BBBB.......",
    "......BBBB...............BBBB.......",
    ".S....BBBB...............BBBB.....S.",
    ".SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS.",
    "....................................",
])

TRAIN = Sprite(48, 16, [
    "................................................",
    "................................................",
    "................................................",
    "BBBBBBBBBBBBBBBBBBBBBB...BBBBBBBBBBBBBBBBBBBBBB.",
    "B....................B...B....................B.",
    "B...BBBBB..BBBBB.....B...B...BBBBB..BBBBB.....LL",
    "B....................B...B....................LL",
    "B....................B...B....................B.",
    "B....................B...B....................B.",
    "B....................B...B....................B.",
    "BBBBBBBBBBBBBBBBBBBBBB...BBBBBBBBBBBBBBBBBBBBBB.",
    "....BBB........BBB...........BBB........BBB.....",
    "....BBB........BBB...........BBB........BBB.....",
    "S...BBB........BBB...........BBB........BBB....S",
    "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS",
    "................................................",
])

COLOURS = {
    "bg":       (10, 22, 40),
    "panel":    (16, 32, 54),
    "panel_hi": (22, 42, 68),
    "line":     (40, 72, 108),
    "text":     (214, 238, 250),
    "dim":      (130, 172, 204),
    "live":     (120, 220, 235),
    "warn":     (255, 190, 110),
    "road":     (20, 40, 64),
    "rail":     (20, 40, 64),
    "window":   (150, 220, 245),
    "dark":     (10, 22, 40),
}

THEME = Theme(
    name="blueprint",
    label="Blueprint",
    colours=COLOURS,
    sprites={"bus": BUS, "train": TRAIN},
    kind_fallback={"bus": (130, 210, 240), "train": (130, 210, 240)},
    roles={"S": (58, 112, 152), "W": (150, 220, 245), "D": (10, 22, 40),
           "L": (255, 255, 255)},
    use_route_color=False,
)
```

- [ ] **Step 3: Register both**

```python
from . import arcade, blueprint, ghibli, transit

_ALL = [transit.THEME, ghibli.THEME, arcade.THEME, blueprint.THEME]
```

- [ ] **Step 4: Run the suite**

Run: `python -m pytest tests/test_themes.py -v`
Expected: PASS across four themes.

Blueprint uses one badge colour for both kinds deliberately — a schematic does
not colour-code by mode. That is fine; no test requires them to differ.

- [ ] **Step 5: Commit**

```bash
git add tools/board/themes/arcade.py tools/board/themes/blueprint.py \
        tools/board/themes/__init__.py
git commit -m "Add Arcade and Blueprint themes"
```

---

### Task 5: Paper and Neon

Paper is the first **light** theme. It is worth its place partly as a test: any
place the code assumed a dark ground will show up here.

**Files:**
- Create: `tools/board/themes/paper.py`, `tools/board/themes/neon.py`
- Modify: `tools/board/themes/__init__.py`

- [ ] **Step 1: Write Paper**

Create `tools/board/themes/paper.py`:

```python
"""Pastel cut-out: soft shapes with thick white keylines, like layered paper.

The only light theme. Anything in the codebase that assumed a dark ground
fails here, which is half the reason it exists.
"""

from ..sprites import Sprite
from .base import Theme

BUS = Sprite(36, 16, [
    "....................................",
    "....................................",
    "..WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW..",
    "...BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB...",
    "W.BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.W",
    "WBBBWWWWWBBWWWWWBBWWWWWBBBBBBBBLLLBW",
    "WBBBWWWWWBBWWWWWBBWWWWWBBBBBBBBLLLBW",
    "WBBBWWWWWBBWWWWWBBWWWWWBBBBBBBBLLLBW",
    "WBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBW",
    "WBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBW",
    "W.BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.W",
    "...BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB...",
    "..WWWWDDDDDWWWWWWWWWWWWWDDDDDWWWWW..",
    "......DDDDD.............DDDDD.......",
    "......DDDDD.............DDDDD.......",
    "....................................",
])

TRAIN = Sprite(48, 16, [
    "................................................",
    "................................................",
    "..WWWWWWWWWWWWWWWWWWW......WWWWWWWWWWWWWWWWWWW..",
    "...BBBBBBBBBBBBBBBBB........BBBBBBBBBBBBBBBBB...",
    "W.BBBBBBBBBBBBBBBBBBB.W..W.BBBBBBBBBBBBBBBBBBB.W",
    "WBBBWWWWWWBBWWWWWWBBBBW..WBBBWWWWWWBBWWWWWWBBLLL",
    "WBBBWWWWWWBBWWWWWWBBBBW..WBBBWWWWWWBBWWWWWWBBLLL",
    "WBBBWWWWWWBBWWWWWWBBBBW..WBBBWWWWWWBBWWWWWWBBLLL",
    "WBBBBBBBBBBBBBBBBBBBBBW..WBBBBBBBBBBBBBBBBBBBBBW",
    "WBBBBBBBBBBBBBBBBBBBBBW..WBBBBBBBBBBBBBBBBBBBBBW",
    "W.BBBBBBBBBBBBBBBBBBB.W..W.BBBBBBBBBBBBBBBBBBB.W",
    "...BBBBBBBBBBBBBBBBB........BBBBBBBBBBBBBBBBB...",
    "..WWWDDDDWWWWWWWDDDDW......WWWDDDDWWWWWWWDDDDW..",
    ".....DDDD.......DDDD..........DDDD.......DDDD...",
    ".....DDDD.......DDDD..........DDDD.......DDDD...",
    "................................................",
])

COLOURS = {
    "bg":       (240, 236, 226),
    "panel":    (255, 252, 246),
    "panel_hi": (247, 242, 233),
    "line":     (222, 214, 200),
    "text":     (48, 42, 38),
    "dim":      (122, 112, 102),
    "live":     (62, 140, 100),
    "warn":     (198, 104, 52),
    "road":     (214, 206, 192),
    "rail":     (214, 206, 192),
    "window":   (255, 252, 246),
    "dark":     (58, 50, 44),
}

THEME = Theme(
    name="paper",
    label="Paper",
    colours=COLOURS,
    sprites={"bus": BUS, "train": TRAIN},
    kind_fallback={"bus": (232, 146, 136), "train": (126, 190, 170)},
    roles={"S": "shade", "W": (255, 252, 246), "D": (82, 72, 66),
           "L": (250, 200, 90)},
    use_route_color=False,
)
```

- [ ] **Step 2: Write Neon**

Create `tools/board/themes/neon.py`:

```python
"""Late-night city: hot magenta and cyan on near-black, edges that glow.

The glow role resolves to a brightened body colour rather than a fixed
amber, so a cyan train glows cyan. That is why role meanings are per-theme.
"""

from ..sprites import Sprite
from .base import Theme

BUS = Sprite(36, 16, [
    "....................................",
    "....................................",
    "....................................",
    "..LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL..",
    "..LBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBL..",
    "..LBBWWWWWBBWWWWWBBWWWWWBBBBBBBLLL..",
    "..LBBWWWWWBBWWWWWBBWWWWWBBBBBBBLLL..",
    "..LBBWWWWWBBWWWWWBBWWWWWBBBBBBBBBL..",
    "..LBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBL..",
    "..LBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBL..",
    "..LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL..",
    "....................................",
    "....SSSSSSSSSSSSSSSSSSSSSSSSSSSS....",
    "........SSSSSSSSSSSSSSSSSSSS........",
    "....................................",
    "....................................",
])

TRAIN = Sprite(48, 16, [
    "................................................",
    "................................................",
    "................................................",
    "LLLLLLLLLLLLLLLLLLLLLL...LLLLLLLLLLLLLLLLLLLLLL.",
    "LBBBBBBBBBBBBBBBBBBBBL...LBBBBBBBBBBBBBBBBBBBBL.",
    "LBBWWWWWWBBWWWWWWBBBBL...LBBWWWWWWBBWWWWWWBBBBLL",
    "LBBWWWWWWBBWWWWWWBBBBL...LBBWWWWWWBBWWWWWWBBBBLL",
    "LBBWWWWWWBBWWWWWWBBBBL...LBBWWWWWWBBWWWWWWBBBBL.",
    "LBBBBBBBBBBBBBBBBBBBBL...LBBBBBBBBBBBBBBBBBBBBL.",
    "LBBBBBBBBBBBBBBBBBBBBL...LBBBBBBBBBBBBBBBBBBBBL.",
    "LLLLLLLLLLLLLLLLLLLLLL...LLLLLLLLLLLLLLLLLLLLLL.",
    "................................................",
    "..SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS..",
    "........SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS........",
    "................................................",
    "................................................",
])

COLOURS = {
    "bg":       (8, 6, 16),
    "panel":    (18, 12, 32),
    "panel_hi": (26, 16, 44),
    "line":     (52, 30, 78),
    "text":     (244, 238, 255),
    "dim":      (158, 138, 196),
    "live":     (60, 235, 180),
    "warn":     (255, 210, 70),
    "road":     (30, 18, 46),
    "rail":     (30, 18, 46),
    "window":   (244, 250, 255),
    "dark":     (18, 8, 28),
}

THEME = Theme(
    name="neon",
    label="Neon",
    colours=COLOURS,
    sprites={"bus": BUS, "train": TRAIN},
    kind_fallback={"bus": (255, 70, 170), "train": (80, 235, 240)},
    roles={"S": "shade", "W": (244, 250, 255), "D": (18, 8, 28),
           "L": "bright"},
    use_route_color=False,
)
```

- [ ] **Step 3: Register both**

```python
from . import arcade, blueprint, ghibli, neon, paper, transit

_ALL = [transit.THEME, ghibli.THEME, arcade.THEME,
        blueprint.THEME, paper.THEME, neon.THEME]
```

- [ ] **Step 4: Run the suite across all six**

Run: `python -m pytest tests/test_themes.py -v`
Expected: PASS across six themes.

Paper is the likely failure: `test_body_text_is_legible_on_the_panel` compares
near-black text on near-white panel, which passes easily, but
`test_secondary_text_is_legible_on_the_panel` on a light ground is tighter.
If `dim` fails, darken it. Never lower the threshold.

- [ ] **Step 5: Commit**

```bash
git add tools/board/themes/paper.py tools/board/themes/neon.py \
        tools/board/themes/__init__.py
git commit -m "Add Paper and Neon themes

Paper is the first light theme, which makes it a test as much as a look:
anywhere the code assumed a dark ground shows up here.

Neon's glow role resolves to a brightened body colour rather than a fixed
amber, so a cyan train glows cyan - the reason role meanings are per-theme
rather than global."
```

---

### Task 6: `--theme` in the simulator, and goldens for the new themes

**Files:**
- Modify: `tools/simulate.py`, `tools/regolden.py`, `tests/test_render.py`, `tests/test_simulate.py`
- Create: `tests/golden/<theme>__<scene>.png` for the five new themes

- [ ] **Step 1: Add `--theme` to the simulator**

In `tools/simulate.py`:

```python
def contact_sheet(names, scale=2, theme=None):
    th = themes.get(theme) if theme else None
    imgs = [(n, render.render(scenes.SCENES[n], theme=th)) for n in names]
    ...


def animate(board, frames=48, fps=12, theme=None):
    th = themes.get(theme) if theme else None
    return [render.render(board, t=i / fps, theme=th) for i in range(frames)]
```

Add the argument and validation:

```python
    ap.add_argument("--theme", default=None,
                    help="theme name, or omit for the board's own")
    ...
    if args.theme and args.theme not in themes.names():
        raise SystemExit(
            f"unknown theme {args.theme!r}. known: {', '.join(themes.names())}")
```

Pass `args.theme` into `contact_sheet` and `animate`, and put the theme name
into output filenames so themes do not overwrite each other:

```python
    suffix = f"_{args.theme}" if args.theme else ""
    sheet_path = os.path.join(OUT, f"scenes{suffix}.png")
    ...
    p = os.path.join(OUT, f"{n}{suffix}.gif")
```

Add to `tools/simulate.py` imports: `from tools.board import render, scenes, themes`.

- [ ] **Step 2: Extend `regolden.py` to cover the new themes**

Replace the body of `main()` in `tools/regolden.py`:

```python
# All scenes for the default theme; two representative scenes for the rest.
# A golden nobody looked at is worthless, so this stays reviewable by hand.
EXTRA_SCENES = ["two_up", "empty"]


def main():
    os.makedirs(GOLDEN, exist_ok=True)
    for name in sorted(scenes.SCENES):
        path = os.path.join(GOLDEN, name + ".png")
        render.render(scenes.SCENES[name], t=0.0).save(path)
        print("wrote", os.path.normpath(path))

    for theme in themes.names():
        if theme == themes.DEFAULT:
            continue
        for scene in EXTRA_SCENES:
            path = os.path.join(GOLDEN, f"{theme}__{scene}.png")
            render.render(scenes.SCENES[scene], t=0.0,
                          theme=themes.get(theme)).save(path)
            print("wrote", os.path.normpath(path))
```

Add `themes` to its import line.

- [ ] **Step 3: Add the themed golden test**

Append to `tests/test_render.py`:

```python
from tools.board import themes

THEMED = [(th, sc) for th in themes.names() if th != themes.DEFAULT
          for sc in ("two_up", "empty")]


@pytest.mark.parametrize("theme,scene", THEMED)
def test_themed_render_matches_golden(theme, scene):
    path = os.path.join(GOLDEN, f"{theme}__{scene}.png")
    assert os.path.exists(path), (
        f"no golden for {theme}/{scene}. Run `python tools/regolden.py` "
        f"and commit the result."
    )
    expected = Image.open(path).convert("RGB")
    actual = render.render(scenes.SCENES[scene], t=0.0, theme=themes.get(theme))
    assert actual.tobytes() == expected.tobytes(), (
        f"render of {theme}/{scene} changed. If intended, run "
        f"`python tools/regolden.py` and commit the new goldens."
    )


@pytest.mark.parametrize("theme", themes.names())
def test_every_theme_renders_every_scene_without_raising(theme):
    for name in ALL:
        img = render.render(scenes.SCENES[name], theme=themes.get(theme))
        assert img.size == (320, 240)
```

- [ ] **Step 4: Run it and confirm it fails**

Run: `python -m pytest tests/test_render.py -k themed -v`
Expected: FAIL on all ten, "no golden for ...".

- [ ] **Step 5: Generate and actually look at them**

```bash
python tools/regolden.py
python tools/simulate.py --scene two_up --theme ghibli
python tools/simulate.py --scene two_up --theme arcade
python tools/simulate.py --scene two_up --theme blueprint
python tools/simulate.py --scene two_up --theme paper
python tools/simulate.py --scene two_up --theme neon
```

Open each. Check specifically:

- **paper**: text is dark on light, the stop marker and track are visible
  against a pale panel, and the "live" dot still reads
- **neon**: the bus glows magenta and the train glows cyan, not both amber
- **ghibli**: the cat-bus's eyes read; the passenger is visible
- **blueprint**: outlined vehicles do not disappear against the panel
- **arcade**: the black outline is visible against the near-black background

Fix any theme that fails this before freezing, then regenerate.

- [ ] **Step 6: Add a simulator test for the flag**

Append to `tests/test_simulate.py`:

```python
from tools.board import themes


def test_contact_sheet_accepts_a_theme():
    a = simulate.contact_sheet(["two_up"], scale=1)
    b = simulate.contact_sheet(["two_up"], scale=1, theme="neon")
    assert a.tobytes() != b.tobytes()


def test_unknown_theme_is_rejected():
    with pytest.raises(KeyError):
        simulate.contact_sheet(["two_up"], scale=1, theme="nope")


@pytest.mark.parametrize("name", themes.names())
def test_every_theme_produces_a_sheet(name):
    assert simulate.contact_sheet(["two_up"], scale=1, theme=name).width >= 320
```

- [ ] **Step 7: Run everything**

Run: `python -m pytest -v`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add tools/simulate.py tools/regolden.py tests/golden \
        tests/test_render.py tests/test_simulate.py
git commit -m "Add --theme to the simulator and freeze themed goldens

All eight scenes for transit, plus two_up and empty for each other theme.
Eighteen goldens rather than forty-eight, because a golden nobody looked at
is worthless and eighteen is the number I can actually review."
```

---

### Task 7: Export every theme to C

**Files:**
- Modify: `tools/export_sprites.py`, `tests/test_export.py`
- Modify: `src/sprites.h` (generated)

Six themes x 2 sprites is roughly 3 KB of flash. There is no reason to ship
anything less than all of them.

- [ ] **Step 1: Update the exporter**

In `tools/export_sprites.py`, replace `render_header()` and the import:

```python
from tools.board import themes  # noqa: E402


def render_header():
    parts = [
        "// generated by tools/export_sprites.py - do not edit by hand",
        "// regenerate after changing any tools/board/themes/*.py",
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
        for kind in ("bus", "train"):
            s = th.sprite(kind)
            tag = f"{n.upper()}_{kind.upper()}"
            parts += [
                "",
                f"#define SPRITE_{tag}_W {s.width}",
                f"#define SPRITE_{tag}_H {s.height}",
                f"#define SPRITE_{tag}_STRIDE {(s.width + 1) // 2}",
                _array(f"SPRITE_{tag}_DATA", pack(s)),
            ]
    return "\n".join(parts) + "\n"
```

Note the per-sprite `_H`: themes no longer share one height, so the single
`SPRITE_H` define is gone.

- [ ] **Step 2: Update the export tests**

Replace the sprite-specific tests in `tests/test_export.py`:

```python
from tools.board import themes

ALL_THEMES = themes.names()


@pytest.mark.parametrize("name", ALL_THEMES)
def test_packed_size_is_two_pixels_per_byte(name):
    for kind, s in themes.get(name).sprites.items():
        assert len(ex.pack(s)) == (s.width + 1) // 2 * s.height


@pytest.mark.parametrize("name", ALL_THEMES)
def test_pack_round_trips_back_to_the_original_grid(name):
    rev = {v: k for k, v in ex.ROLE_INDEX.items()}
    for kind, s in themes.get(name).sprites.items():
        data = ex.pack(s)
        stride = (s.width + 1) // 2
        for y, row in enumerate(s.rows):
            for x, ch in enumerate(row):
                byte = data[y * stride + x // 2]
                nib = (byte >> 4) if x % 2 == 0 else (byte & 0x0F)
                assert rev[nib] == ch, f"{name}/{kind} mismatch at ({x},{y})"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_header_declares_every_theme(name):
    h = ex.render_header()
    assert f"THEME_{name.upper()}" in h
    assert f"SPRITE_{name.upper()}_BUS_DATA" in h
    assert f"SPRITE_{name.upper()}_TRAIN_DATA" in h


def test_header_declares_the_theme_count():
    assert f"#define THEME_COUNT {len(ALL_THEMES)}" in ex.render_header()


@pytest.mark.parametrize("name", ALL_THEMES)
def test_header_dimensions_match_python(name):
    h = ex.render_header()
    for kind, s in themes.get(name).sprites.items():
        assert f"SPRITE_{name.upper()}_{kind.upper()}_W {s.width}" in h
        assert f"SPRITE_{name.upper()}_{kind.upper()}_H {s.height}" in h
```

Keep `test_transparent_is_index_zero`, `test_every_role_has_an_index`,
`test_indices_fit_in_a_nibble`, `test_header_has_an_include_guard`,
`test_header_warns_against_hand_editing`, and
`test_role_indices_are_exported_for_the_firmware_palette` unchanged.

- [ ] **Step 3: Run the export tests**

Run: `python -m pytest tests/test_export.py -v`
Expected: PASS.

- [ ] **Step 4: Regenerate the header and check its size**

Run: `python tools/export_sprites.py`
Expected: roughly 12-14 KB of source, about 3 KB of actual array data.

Confirm `THEME_COUNT 6` and twelve `_DATA` arrays:

```bash
grep -c "_DATA\[" src/sprites.h     # expect 12
grep "THEME_COUNT" src/sprites.h    # expect 6
```

- [ ] **Step 5: Run everything and update the README**

Run: `python -m pytest`

In `README.md`, add to the Layout table:

```markdown
| Themes (palette + sprites) | `tools/board/themes/` |
```

And under "Try it without hardware":

```markdown
Six themes ship: `transit`, `ghibli`, `arcade`, `blueprint`, `paper`, `neon`.

```bash
python tools/simulate.py --scene two_up --theme ghibli
```
```

- [ ] **Step 6: Commit**

```bash
git add tools/export_sprites.py src/sprites.h tests/test_export.py README.md
git commit -m "Export all six themes to C

About 3KB of flash for every theme's sprites, so there is no reason to ship
fewer. Per-sprite heights replace the single SPRITE_H, since themes no
longer share one."
```

---

## Self-Review

**Spec coverage.** The in-chat design's six points all land: theme controls
palette + sprites (Task 1), per-theme role meanings (Task 1 `Theme.role_colours`),
`use_route_color` (Task 1, transit only), parametrised cross-theme tests
(Task 2), contrast checks (Task 2), reduced golden coverage (Task 6), C export
of all themes (Task 7).

**Placeholder scan.** One deliberate `# ... copied verbatim` in Task 1 Step 5:
the transit art must be moved byte-identically and retyping it is the specific
risk that would break the goldens. The instruction says copy, not author, and
Step 10 verifies it. Every other code block is complete. All ten new sprites
are full verified art, authored on a char grid and visually reviewed before
this plan was written.

**Type consistency.** `Theme.colour/badge_colour/role_colours/sprite` used
identically in Tasks 1, 3-7. `sprites.blit(draw, sprite, x, y, colours)` — new
signature — used consistently in Task 1 Step 7 and Step 9.
`layout.vehicle_x(eta_s, lane, sprite_width)` changed in Step 8 and every call
site updated in Step 8 and Step 9. `render.render(board, t, theme)` consistent
across Tasks 1, 6. `themes.names()`/`get()`/`DEFAULT`/`all_themes()` consistent
throughout.

**Known risks.**
1. Task 1 is the only risky task — a large refactor whose success criterion is
   that nothing changes. The unregenerated goldens are the check; if they fail,
   diff rather than regenerate.
2. `paper` may fail the `dim`-on-`panel` contrast check. The fix is the theme's
   colours, never the threshold.
3. `Watch.colour` is deleted in Task 1 Step 9. Any missed call site raises
   `AttributeError` immediately rather than rendering wrongly.
