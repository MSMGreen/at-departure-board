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
