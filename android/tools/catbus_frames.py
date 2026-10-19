"""Draw the widget's bobbing cat-bus from the board's own sprite.

The cat-bus is the ghibli theme's compact bus (tools/board/art), in the
theme's bus amber. Two frames: as drawn, and with the body one pixel lower
and every other leg lifted, so that flipping between them reads as a trot.
Both are scaled by whole pixels, so the launcher shows them unfiltered.

Run from the repository root:  python android/tools/catbus_frames.py
"""

import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)

from tools.board import themes  # noqa: E402
from tools.board.sprites import Sprite, blit  # noqa: E402

SCALE = 3  # about 40 x 19 dp at 450 dpi
OUT = os.path.join(ROOT, "android", "app", "src", "main", "res", "drawable-nodpi")


def legs_from(rows):
    """Index of the first row that is legs only."""
    for i, row in enumerate(rows):
        if set(row) <= {".", "S"} and "S" in row:
            return i
    raise ValueError("no leg rows in the sprite")


def trot(sprite):
    """The body a pixel lower, alternate legs lifted, the feet left planted."""
    rows = sprite.rows
    top = legs_from(rows)
    body, legs = rows[:top], rows[top:]
    blank = "." * sprite.width
    feet = max(i for i, row in enumerate(legs) if "S" in row)
    # Legs are runs of S; number them left to right in the bottom leg row.
    starts = [x for x, c in enumerate(legs[feet]) if c == "S" and (x == 0 or legs[feet][x - 1] != "S")]
    lifted = set()
    for n, x in enumerate(starts):
        if n % 2:
            x2 = x
            while x2 < sprite.width and legs[feet][x2] == "S":
                lifted.add(x2)
                x2 += 1
    out_legs = []
    for i, row in enumerate(legs[1:feet + 1], start=1):
        # The body moved down a row, so each leg loses its top row; a lifted
        # leg also loses its foot.
        if i == feet:
            row = "".join("." if x in lifted else c for x, c in enumerate(row))
        out_legs.append(row)
    rows2 = [blank] + body + out_legs + legs[feet + 1:]
    return Sprite(sprite.width, len(rows2), rows2)


def save(sprite, colours, name):
    img = Image.new("RGBA", (sprite.width, sprite.height), (0, 0, 0, 0))
    blit(ImageDraw.Draw(img), sprite, 0, sprite.height, colours)
    img = img.resize((sprite.width * SCALE, sprite.height * SCALE), Image.NEAREST)
    img.save(os.path.join(OUT, name), optimize=True)


def main():
    theme = themes.get("ghibli")
    colours = theme.role_colours(theme.kind_fallback["bus"])
    sprite = theme.sprite("compact", "bus")
    os.makedirs(OUT, exist_ok=True)
    save(sprite, colours, "catbus_1.png")
    save(trot(sprite), colours, "catbus_2.png")


if __name__ == "__main__":
    main()
