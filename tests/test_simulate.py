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
