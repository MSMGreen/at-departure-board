"""Colour roles for the board.

Sprites never name a colour; they name a role. That is what lets one bus sprite
render in whatever colour the route actually is.
"""

# Role letter -> what it means in a sprite grid.
ROLES = {
    ".": "transparent",
    "B": "body",     # the route's colour
    "S": "shade",    # body, darkened - the underside
    "W": "window",
    "D": "dark",     # wheels, bogies
    "L": "light",    # headlight
}

BASE = {
    "bg":       (12, 16, 24),
    "panel":    (22, 29, 42),
    "panel_hi": (32, 42, 60),
    "line":     (44, 57, 78),
    "text":     (233, 238, 245),
    "dim":      (128, 143, 165),
    "live":     (120, 220, 130),
    "warn":     (245, 176, 66),
    "road":     (38, 44, 55),
    "rail":     (58, 50, 44),
    "window":   (210, 240, 255),
    "dark":     (20, 24, 30),
}

# Used when AT supplies no route_color, which for buses is always.
KIND_FALLBACK = {
    "bus":   (0, 168, 224),
    "train": (151, 201, 61),
}


def kind_colour(kind):
    return KIND_FALLBACK.get(kind, BASE["dim"])


def parse_hex(value):
    """'97C93D' or '#97C93D' -> (151, 201, 61). None if unusable."""
    if not value:
        return None
    v = value.strip().lstrip("#")
    if len(v) != 6:
        return None
    try:
        rgb = tuple(int(v[i:i + 2], 16) for i in (0, 2, 4))
    except ValueError:
        return None
    if rgb == (0, 0, 0):
        return None  # HUIA's #000000 - invisible on our ground, treat as absent
    return rgb


def resolve_badge_colour(kind, route_color=None):
    return parse_hex(route_color) or kind_colour(kind)


def shade(rgb, factor=0.55):
    return tuple(max(0, min(255, int(c * factor))) for c in rgb)
