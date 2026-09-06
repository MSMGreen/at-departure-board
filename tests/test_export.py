import pytest

from tools import export_sprites as ex
from tools.board import palette, themes

TRANSIT = themes.get("transit")


def _sprite(kind):
    return TRANSIT.sprite("compact", kind)


def test_transparent_is_index_zero():
    assert ex.ROLE_INDEX["."] == 0


def test_every_role_has_an_index():
    assert set(ex.ROLE_INDEX) == set(palette.ROLES)


def test_indices_fit_in_a_nibble():
    assert all(0 <= v <= 15 for v in ex.ROLE_INDEX.values())


@pytest.mark.parametrize("name", ["bus", "train"])
def test_packed_size_is_two_pixels_per_byte(name):
    s = _sprite(name)
    assert len(ex.pack(s)) == (s.width + 1) // 2 * s.height


@pytest.mark.parametrize("name", ["bus", "train"])
def test_pack_round_trips_back_to_the_original_grid(name):
    s = _sprite(name)
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
    assert f"SPRITE_BUS_W {_sprite('bus').width}" in h
    assert f"SPRITE_TRAIN_W {_sprite('train').width}" in h
    assert f"SPRITE_H {_sprite('bus').height}" in h


def test_header_has_an_include_guard():
    assert "#pragma once" in ex.render_header()


def test_header_warns_against_hand_editing():
    assert "generated" in ex.render_header().lower()


def test_role_indices_are_exported_for_the_firmware_palette():
    h = ex.render_header()
    assert "ROLE_BODY" in h and "ROLE_WINDOW" in h
