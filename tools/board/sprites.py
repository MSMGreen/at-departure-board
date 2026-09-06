"""Pixel-art vehicles, authored as role grids.

Every sprite is 14 rows. Origin when blitting is the BOTTOM-left, because
vehicles sit on a track line and that is the edge that must stay put.
Headlights sit on the right: everything travels left-to-right toward its stop.
"""

from dataclasses import dataclass
from typing import List

from . import palette


@dataclass(frozen=True)
class Sprite:
    width: int
    height: int
    rows: List[str]


BUS = Sprite(34, 14, [
    "...BBBBBBBBBBBBBBBBBBBBBBBBBBBB...",
    "..BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB..",
    ".BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.",
    "BBWWWWBBWWWWBBWWWWBBWWWWBBBBBBBBBB",
    "BBWWWWBBWWWWBBWWWWBBWWWWBBBBBBBBBB",
    "BBWWWWBBWWWWBBWWWWBBWWWWBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBLL",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBLL",
    "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS",
    "....DDDD..........DDDD............",
    "...DDDDDD........DDDDDD...........",
    "...DDDDDD........DDDDDD...........",
    "....DDDD..........DDDD............",
    "..................................",
])

TRAIN = Sprite(48, 14, [
    "...BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB...",
    "..BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB..",
    ".BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.",
    "BBWWWWWWBBWWWWWWBBBBWWWWWWBBWWWWWWBBBBBBBBBBBBBB",
    "BBWWWWWWBBWWWWWWBBBBWWWWWWBBWWWWWWBBBBBBBBBBBBBB",
    "BBWWWWWWBBWWWWWWBBBBWWWWWWBBWWWWWWBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBLL",
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBLL",
    "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS",
    "....DDDD......DDDD........DDDD......DDDD........",
    "....DDDD......DDDD........DDDD......DDDD........",
    ".....DD........DD..........DD........DD.........",
    "................................................",
    "................................................",
])

SPRITES = {"bus": BUS, "train": TRAIN}


def role_colours(body):
    """Map role letters to concrete RGB for one vehicle colour."""
    return {
        "B": body,
        "S": palette.shade(body),
        "W": palette.BASE["window"],
        "D": palette.BASE["dark"],
        "L": palette.BASE["warn"],
    }


def blit(draw, name, x, y, body):
    """Draw sprite `name` with its BOTTOM-left corner at (x, y)."""
    s = SPRITES[name]
    colours = role_colours(body)
    top = int(y) - s.height
    x = int(x)
    for ry, row in enumerate(s.rows):
        py = top + ry
        run_start, run_role = None, None
        for rx in range(s.width + 1):
            role = row[rx] if rx < s.width else None
            if role != run_role:
                if run_role in colours and run_start is not None:
                    draw.rectangle(
                        (x + run_start, py, x + rx - 1, py),
                        fill=colours[run_role],
                    )
                run_start, run_role = rx, role
