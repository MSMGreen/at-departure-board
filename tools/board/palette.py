"""Colour roles for the board.

Sprites never name a colour; they name a role. That is what lets one bus sprite
render in whatever colour the route actually is.

What each role *means* is a theme's business - see `themes/`. This module only
fixes the vocabulary and the arithmetic for deriving one colour from another.
"""

# Role letter -> what it means in a sprite grid.
ROLES = {
    ".": "transparent",
    "B": "body",        # the vehicle's colour
    "H": "highlight",   # body, lit from above
    "S": "shade",       # body, darkened
    "M": "midshadow",   # the underside
    "W": "window",
    "G": "glint",       # specular dot on glass
    "D": "outline",
    "K": "detail",      # wheels, pupils, ironwork
    "L": "lamp",
    "A": "accent",      # trim, roof, destination blind
}


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


def shade(rgb, factor=0.55):
    return tuple(max(0, min(255, int(c * factor))) for c in rgb)


def bright(rgb, factor=1.6, floor=40):
    """Brighten toward a glow. Neon-style themes make their halo from this."""
    return tuple(min(255, int(c * factor) + floor) for c in rgb)
