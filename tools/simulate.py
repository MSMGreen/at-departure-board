#!/usr/bin/env python3
"""Preview the board without hardware.

    python tools/simulate.py --all --scale 2 --show
    python tools/simulate.py --scene arriving --gif
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.board import render, scenes  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")


def _label_font(size=15):
    try:
        return ImageFont.truetype(
            os.path.join(os.environ.get("WINDIR", "C:/Windows"),
                         "Fonts", "arialbd.ttf"), size)
    except OSError:
        return ImageFont.load_default()


def contact_sheet(names, scale=2):
    """Every named scene side by side, NEAREST-upscaled to show real pixels."""
    imgs = [(n, render.render(scenes.SCENES[n])) for n in names]  # KeyError if unknown
    pad, lab = 12, 22
    cw, ch = 320 * scale, 240 * scale
    sheet = Image.new("RGB", (pad + len(imgs) * (cw + pad), pad + lab + ch + pad),
                      (18, 18, 20))
    d = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(imgs):
        x = pad + i * (cw + pad)
        sheet.paste(im.resize((cw, ch), Image.NEAREST), (x, pad + lab))
        d.text((x, pad + 4), name, font=_label_font(), fill=(235, 235, 240))
    return sheet


def animate(board, frames=48, fps=12):
    return [render.render(board, t=i / fps) for i in range(frames)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", default=None, help="scene name, or omit for --all")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--gif", action="store_true")
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--show", action="store_true")
    args = ap.parse_args()

    if args.scene and args.scene not in scenes.SCENES:
        raise SystemExit(
            f"unknown scene {args.scene!r}. known: {', '.join(sorted(scenes.SCENES))}")

    os.makedirs(OUT, exist_ok=True)
    names = sorted(scenes.SCENES) if (args.all or not args.scene) else [args.scene]

    sheet = contact_sheet(names, args.scale)
    sheet_path = os.path.join(OUT, "scenes.png")
    sheet.save(sheet_path)
    print("wrote", sheet_path)

    if args.gif:
        for n in names:
            frames = [f.resize((640, 480), Image.NEAREST)
                      for f in animate(scenes.SCENES[n])]
            p = os.path.join(OUT, f"{n}.gif")
            frames[0].save(p, save_all=True, append_images=frames[1:],
                           duration=80, loop=0, optimize=True)
            print("wrote", p)

    if args.show:
        sheet.show()


if __name__ == "__main__":
    main()
