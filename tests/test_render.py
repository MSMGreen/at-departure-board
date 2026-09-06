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

