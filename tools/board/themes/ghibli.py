"""Ghibli-flavoured night: warm lanterns on deep indigo.

A cat-shaped bus with too many legs, and a night train crossing water with a
tall quiet passenger aboard. Original art, not reproductions.
"""

from .. import scenery
from ..art import load
from .base import Theme

COLOURS = {
    "bg":       (14, 12, 34),
    "panel":    (26, 22, 50),
    "panel_hi": (36, 31, 64),
    "line":     (54, 46, 86),
    "text":     (250, 240, 220),
    "dim":      (168, 156, 190),
    "live":     (150, 210, 140),
    "warn":     (255, 190, 90),
    "road":     (44, 36, 60),
    "rail":     (52, 40, 52),
    "window":   (255, 248, 226),
    "dark":     (24, 18, 34),
}

THEME = Theme(
    name="ghibli",
    label="Ghibli Night",
    colours=COLOURS,
    sprites=load("ghibli"),
    kind_fallback={"bus": (232, 168, 74), "train": (150, 178, 226)},
    roles={"H": "bright", "S": "shade", "M": "shade",
           "W": (255, 248, 226), "G": (255, 255, 255),
           "D": (28, 18, 30), "K": (36, 22, 34),
           "L": (255, 210, 120), "A": (196, 132, 60)},
    use_route_color=False,
    scenery=scenery.ghibli,
)
