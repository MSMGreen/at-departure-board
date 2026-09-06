"""Per-theme backgrounds.

A vehicle alone in a 111px lane looks abandoned. Fifteen lines of scenery is
the difference between a sprite on a rectangle and a board you want on a wall.

Every function takes a seeded Rng so a lane's stars and buildings stay put
between frames - scenery must never shimmer.

Scenery ink is derived from the lane's own card colour, never hardcoded. Lanes
alternate between `panel` and `panel_hi`, so a fixed RGB that reads on one is
invisible on the other - the first draft of this module drew overhead wires at
a contrast ratio of 1.001 against panel_hi, which is to say it drew nothing.

The Rng is deliberately not `random.Random`. The firmware repaints the sprite's
bounding box to animate the bob, that box overlaps this scenery, so the ESP32
has to place the same stars the renderer did. A Mersenne Twister cannot be
reproduced in four lines of C; a 32-bit LCG can. `tests/test_render.py` pins
its output.
"""

import math

from . import palette

MASK = 0xFFFFFFFF


class Rng:
    """Numerical Recipes' 32-bit LCG. Portable to C as a bare uint32_t."""

    def __init__(self, seed):
        self.state = (seed * 1664525 + 1013904223) & MASK

    def next(self):
        self.state = (self.state * 1664525 + 1013904223) & MASK
        return self.state

    def below(self, n):
        """0 <= result < n, for n up to 65535.

        Takes the high 16 bits: the low bits of an LCG are famously weak, and
        `next() % n` would draw the stars in visible diagonal stripes.
        """
        return ((self.next() >> 16) * n) >> 16

    def between(self, lo, hi):
        """Inclusive of both ends, matching random.randint."""
        return lo + self.below(hi - lo + 1)

    def chance(self, percent):
        return self.below(100) < percent

    def pick(self, seq):
        return seq[self.below(len(seq))]


def _hill(dx):
    """Height of the Ghibli hill at dx pixels from the lane's left edge.

    Kept as a pure function of dx so the firmware can precompute it into a
    byte table once per lane instead of calling sin() per column.
    """
    return int(7 + 5 * math.sin(dx / 26.0) + 3 * math.sin(dx / 9.0))


def _shore(dx):
    return int(4 + 3 * math.sin(dx / 18.0))


def transit(d, rect, kind, seed, th, card):
    """Low city skyline for the road; overhead wires for the rail."""
    x0, y0, x1, y1 = rect
    rnd = Rng(seed)
    ink = palette.bright(card, 1.0, 22)       # ~1.35 contrast on either panel
    lit = palette.bright(card, 1.3, 52)
    base = y1 - 14
    if kind == "bus":
        x = x0 + 6
        while x < x1 - 6:
            bw, bh = rnd.between(9, 20), rnd.between(6, 18)
            d.rectangle((x, base - bh, x + bw, base), fill=ink)
            for wy in range(base - bh + 3, base - 2, 5):
                for wx in range(x + 2, x + bw - 2, 5):
                    if rnd.chance(35):
                        d.point((wx, wy), fill=lit)
            x += bw + rnd.between(2, 6)
    else:
        d.line((x0 + 6, y0 + 30, x1 - 6, y0 + 30), fill=ink)
        for mx in range(x0 + 20, x1 - 10, 46):
            d.line((mx, y0 + 30, mx, base), fill=ink)


def ghibli(d, rect, kind, seed, th, card):
    """Stars over a hill for the road; stars over water for the rail."""
    x0, y0, x1, y1 = rect
    rnd = Rng(seed)
    stars = [palette.bright(card, 1.2, 58),
             palette.bright(card, 1.4, 92),
             palette.bright(card, 1.6, 140)]
    for _ in range(26):
        d.point((rnd.between(x0 + 6, x1 - 6), rnd.between(y0 + 26, y1 - 34)),
                fill=rnd.pick(stars))
    hill = palette.shade(card, 0.72)           # hills sit BELOW the ground
    base = y1 - 14
    if kind == "bus":
        for x in range(x0 + 6, x1 - 6):
            d.line((x, base - _hill(x - x0), x, base), fill=hill)
    else:
        for x in range(x0 + 6, x1 - 6):
            d.line((x, base - _shore(x - x0), x, base), fill=hill)
        for k in range(6):                     # moon path on the water
            d.line((x0 + 40 + k * 30, base - 2, x0 + 52 + k * 30, base - 2),
                   fill=palette.bright(card, 1.0, 26))
