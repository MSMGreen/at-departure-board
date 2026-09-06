import pytest

from tools.board import palette, themes


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


REQUIRED_COLOURS = ["bg", "panel", "panel_hi", "line", "text", "dim",
                    "live", "warn", "road", "rail", "window", "dark"]
ALL_THEMES = [t.name for t in themes.all_themes()]
SIZES = {"large": 44, "compact": 18}
MAX_W = 100


def _lum(rgb):
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
    assert [k for k in REQUIRED_COLOURS if k not in t.colours] == []


@pytest.mark.parametrize("name", ALL_THEMES)
def test_has_all_four_sprites(name):
    t = themes.get(name)
    for size in SIZES:
        for kind in ("bus", "train"):
            assert t.sprite(size, kind) is not None


@pytest.mark.parametrize("name", ALL_THEMES)
def test_sprite_rows_match_declared_width(name):
    t = themes.get(name)
    for (size, kind), s in t.sprites.items():
        bad = [(i, len(r)) for i, r in enumerate(s.rows) if len(r) != s.width]
        assert bad == [], f"{name}/{size}/{kind}: {bad}"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_sprites_respect_their_size_class(name):
    # compact's 18px is geometric: at n=4 the lane is 55px, the badge ends at
    # y0+24 and the track sits at y0+43.
    t = themes.get(name)
    for (size, kind), s in t.sprites.items():
        assert s.height <= SIZES[size], f"{name}/{size}/{kind} is {s.height}px"
        assert s.width <= MAX_W, f"{name}/{size}/{kind} is {s.width}px wide"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_large_art_actually_uses_its_budget(name):
    # If large art is not meaningfully bigger than compact, the size class is
    # pointless and someone forgot to author it.
    t = themes.get(name)
    for kind in ("bus", "train"):
        assert t.sprite("large", kind).height >= t.sprite("compact", kind).height + 12


@pytest.mark.parametrize("name", ALL_THEMES)
def test_only_known_roles_are_used(name):
    t = themes.get(name)
    for (size, kind), s in t.sprites.items():
        used = {c for row in s.rows for c in row}
        assert used <= set(palette.ROLES), f"{name}/{size}/{kind}"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_every_role_used_resolves_to_a_colour(name):
    t = themes.get(name)
    cols = t.role_colours((120, 120, 120))
    for (size, kind), s in t.sprites.items():
        used = {c for row in s.rows for c in row} - {"."}
        assert not (used - set(cols)), f"{name}/{size}/{kind}: {used - set(cols)}"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_there_is_a_light_at_the_leading_edge(name):
    # Direction of travel must read. This asserts a light EXISTS at the front,
    # not that lights appear only there - the ghibli train is lantern-lit
    # along its whole length by design.
    t = themes.get(name)
    for (size, kind), s in t.sprites.items():
        xs = [x for row in s.rows for x, c in enumerate(row) if c == "L"]
        assert xs, f"{name}/{size}/{kind} has no light"
        assert max(xs) >= s.width - 4, f"{name}/{size}/{kind} light not at front"


# --- legibility ----------------------------------------------------------
# If one of these fails, change the theme's colours, never the threshold.

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
        assert contrast(t.colour("dark"), fill) >= 3.0, f"{name}/{kind}"


@pytest.mark.parametrize("name", ALL_THEMES)
def test_alternating_lanes_are_distinguishable(name):
    t = themes.get(name)
    assert contrast(t.colour("panel"), t.colour("panel_hi")) >= 1.05
