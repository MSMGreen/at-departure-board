#!/usr/bin/env python3
"""Regenerate the committed golden images.

Run this ONLY when a render change is intended, and commit the resulting
PNGs in the same commit as the change that caused them.

    python tools/regolden.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.board import render, scenes  # noqa: E402

GOLDEN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "tests", "golden")


def main():
    os.makedirs(GOLDEN, exist_ok=True)
    for name in sorted(scenes.SCENES):
        path = os.path.join(GOLDEN, name + ".png")
        render.render(scenes.SCENES[name], t=0.0).save(path)
        print("wrote", os.path.normpath(path))


if __name__ == "__main__":
    main()
