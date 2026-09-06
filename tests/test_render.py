import os

import pytest
from PIL import Image

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


# --- transitional: proof that the Task 1 theme refactor changed nothing ----
#
# Task 1 planned to prove itself by leaving the goldens untouched. That could
# never work: the goldens were frozen at 859a810, the art was redesigned at
# 56039af, and Task 1 is simply the commit where render.py starts consuming
# the new art. So the goldens legitimately change here.
#
# This is the replacement proof, and it is a sharper one: every pixel that
# changed must lie in a lane's sprite band. If the refactor had disturbed a
# colour role, a font, the badge or the status bar, pixels would differ
# outside those bands and this would fail.
#
# DELETE THIS IN TASK 3. Task 3 swaps in large art at 1-2 lanes, which moves
# the sprite band by design and makes the comparison meaningless.

PRE_REFACTOR = "859a810"       # "Freeze golden images for every board state"
BAND_ABOVE = 18                # tallest compact sprite (16) + 1px of bob
BAND_BELOW = 2


def _blob(sha, path):
    import subprocess
    r = subprocess.run(["git", "show", f"{sha}:{path}"],
                       capture_output=True)
    return r.stdout if r.returncode == 0 and r.stdout else None


@pytest.mark.parametrize("name", ALL)
def test_the_theme_refactor_only_moved_pixels_in_the_sprite_band(name):
    import io as _io

    from tools.board import layout

    raw = _blob(PRE_REFACTOR, f"tests/golden/{name}.png")
    if raw is None:
        pytest.skip(f"pre-refactor golden for {name} not reachable in git")

    before = Image.open(_io.BytesIO(raw)).convert("RGB")
    after = render.render(scenes.SCENES[name], t=0.0)

    board = scenes.SCENES[name]
    n = len(board.watches)
    bands = []
    for i in range(n):
        ln = layout.lane(i, n)
        bands.append((ln.sprite_baseline - BAND_ABOVE,
                      ln.sprite_baseline + BAND_BELOW))

    px_before, px_after = before.load(), after.load()
    stray = []
    for y in range(before.height):
        if any(lo <= y <= hi for lo, hi in bands):
            continue
        for x in range(before.width):
            if px_before[x, y] != px_after[x, y]:
                stray.append((x, y))
                if len(stray) > 8:
                    break
        if len(stray) > 8:
            break

    assert stray == [], (
        f"{name}: the theme refactor changed {len(stray)}+ pixels outside the "
        f"sprite bands {bands}, first at {stray[:8]}. The refactor was meant "
        f"to change only which sprite is drawn.")
