import pytest

from tools import export_sprites as ex
from tools.board import palette, themes
from tools.board import scenes  # noqa: E402

ALL_THEMES = themes.names()


def test_transparent_is_index_zero():
    assert ex.ROLE_INDEX["."] == 0


def test_every_role_has_an_index():
    assert set(ex.ROLE_INDEX) == set(palette.ROLES)


def test_indices_fit_in_a_nibble():
    assert all(0 <= v <= 15 for v in ex.ROLE_INDEX.values())


@pytest.mark.parametrize("name", ALL_THEMES)
def test_packed_size_is_two_pixels_per_byte(name):
    for (size, kind), s in themes.get(name).sprites.items():
        assert len(ex.pack(s)) == (s.width + 1) // 2 * s.height, \
            f"{name}/{size}/{kind}"


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


@pytest.mark.parametrize("name", ALL_THEMES)
def test_header_dimensions_match_python(name):
    h = ex.render_header()
    for (size, kind), s in themes.get(name).sprites.items():
        tag = f"{name.upper()}_{size.upper()}_{kind.upper()}"
        assert f"#define SPRITE_{tag}_W {s.width}" in h
        assert f"#define SPRITE_{tag}_H {s.height}" in h


def test_all_eleven_roles_are_exported():
    h = ex.render_header()
    for const in ex.ROLE_CONST.values():
        assert const in h


def test_header_has_an_include_guard():
    assert "#pragma once" in ex.render_header()


def test_header_warns_against_hand_editing():
    assert "generated" in ex.render_header().lower()


def test_every_theme_defines_exactly_the_exported_colour_keys():
    for name in ALL_THEMES:
        assert set(themes.get(name).colours) == set(ex.COLOUR_KEYS), name


def test_theme_header_pins_the_colour_enum_order():
    # The C enum is hand-written; these asserts make a mismatch a compile error.
    h = ex.render_theme_header()
    for i, key in enumerate(ex.COLOUR_KEYS):
        assert f"static_assert(C_{key.upper()} == {i}," in h


def test_theme_header_lists_themes_in_sprite_header_order():
    h = ex.render_theme_header()
    positions = [h.index(f'"{name}"') for name in ALL_THEMES]
    assert positions == sorted(positions)


def test_theme_header_warns_against_hand_editing():
    assert "generated" in ex.render_theme_header().lower()


GENERATED = [
    (ex.OUT, ex.render_header),
    (ex.THEME_OUT, ex.render_theme_header),
    (ex.SCENES_OUT, ex.render_scenes_header),
]


@pytest.mark.parametrize("path,render", GENERATED,
                         ids=[p for p, _ in GENERATED])
def test_generated_file_is_current(path, render):
    # Text mode: git may have checked the file out with CRLF.
    with open(path, encoding="utf-8") as fh:
        on_disk = fh.read()
    assert on_disk == render(), \
        f"{path} is stale - run: python tools/export_sprites.py"


def test_scenes_header_has_every_scene_in_order():
    h = ex.render_scenes_header()
    positions = [h.index(f'"{name}"') for name in scenes.SCENES]
    assert positions == sorted(positions)
    assert f"#define SCENE_DATA_COUNT {len(scenes.SCENES)}" in h


def test_cstr_refuses_text_the_panel_font_cannot_draw():
    # The GLCD font has no macron. Catch it here, not as a blank on the glass.
    with pytest.raises(ValueError):
        ex._cstr("to Waitematā")


def test_cstr_escapes_quotes_and_passes_none_as_null():
    assert ex._cstr('say "hi"') == '"say \\"hi\\""'
    assert ex._cstr(None) == "nullptr"
