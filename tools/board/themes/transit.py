"""The default look: a transit board. The only theme that honours AT's own
route colours, because it is the only one pretending to be signage."""

from .. import scenery
from ..art import load
from .base import Theme

COLOURS = {
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
    "window":   (214, 242, 255),
    "dark":     (20, 24, 30),
}

THEME = Theme(
    name="transit",
    label="Transit",
    colours=COLOURS,
    sprites=load("transit"),
    kind_fallback={"bus": (0, 168, 224), "train": (151, 201, 61)},
    roles={"H": "bright", "S": "shade", "M": "shade",
           "W": (214, 242, 255), "G": (255, 255, 255),
           "D": (10, 20, 30), "K": (16, 26, 36),
           "L": (250, 200, 70), "A": (0, 140, 190)},
    use_route_color=True,
    scenery=scenery.transit,
)
