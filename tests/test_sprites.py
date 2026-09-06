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
