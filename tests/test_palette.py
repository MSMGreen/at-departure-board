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
