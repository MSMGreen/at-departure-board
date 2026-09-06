import os

import pytest
from PIL import Image

from tools.board import render, scenes, themes
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


def test_the_scenery_rng_is_portable_to_c():
    # Pinned so the firmware can be checked against these exact numbers.
    # If this test changes, src/ and the goldens both have to change with it.
    from tools.board import scenery
    r = scenery.Rng(7)
    assert [r.below(20) for _ in range(8)] == [18, 12, 18, 0, 17, 7, 1, 1]
    assert scenery.Rng(7).next() == 3923423697


def test_different_lanes_get_different_scenery():
    from tools.board import scenery
    a = [scenery.Rng(7).below(20) for _ in range(8)]
    b = [scenery.Rng(8).below(20) for _ in range(8)]
    assert a != b

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
    b = Board([Watch("20", "to town", "bus" if i % 2 == 0 else "train",
                     [Departure(240 * (i + 1))]) for i in range(n)], "17:42")
    assert render.render(b, theme=themes.get(theme)).size == (320, 240)
