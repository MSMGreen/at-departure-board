"""Load generated sprite data into Sprite objects.

The data is produced by tools/author_art.py. Never edit sprites_data.py.
"""

from ..sprites import Sprite
from .sprites_data import SPRITES as _RAW


def load(theme):
    """All sprites for one theme, keyed (size, kind)."""
    out = {}
    for (th, size, kind), (w, h, rows) in _RAW.items():
        if th == theme:
            out[(size, kind)] = Sprite(w, h, rows)
    if not out:
        raise KeyError(f"no generated art for theme {theme!r}")
    return out


def themes_with_art():
    return sorted({t for (t, _, _) in _RAW})
