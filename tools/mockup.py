#!/usr/bin/env python3
"""Layout mockups for the AT departure board (ILI9341, 320x240).

Renders candidate screen layouts at true 1:1 pixel size, then upscales with
NEAREST so you see the real pixel grid you'll actually get on the TFT.

    python tools/mockup.py            # write PNGs to tools/out/ and open them
    python tools/mockup.py --no-show  # just write files
    python tools/mockup.py --gif      # also render each layout as a GIF
"""

import argparse
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 320, 240
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

# ---------------------------------------------------------------- palette ---
# Deliberately few colours, high contrast at arm's length across a room.
C = {
    "bg":       (12, 16, 24),
    "panel":    (22, 29, 42),
    "panel_hi": (32, 42, 60),
    "line":     (44, 57, 78),
    "text":     (233, 238, 245),
    "dim":      (128, 143, 165),
    "bus":      (0, 168, 224),   # AT-ish cyan for the bus
    "train":    (86, 176, 74),   # Western / E-W green
    "live":     (120, 220, 130),
    "warn":     (245, 176, 66),
    "road":     (38, 44, 55),
    "rail":     (58, 50, 44),
}

FONT_DIR = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")
_fonts = {}


def font(name, size):
    key = (name, size)
    if key not in _fonts:
        try:
            _fonts[key] = ImageFont.truetype(os.path.join(FONT_DIR, name), size)
        except OSError:
            _fonts[key] = ImageFont.load_default()
    return _fonts[key]


def BOLD(s):
    return font("arialbd.ttf", s)


def REG(s):
    return font("arial.ttf", s)


def MONO(s):
    return font("consolab.ttf", s)


# ---------------------------------------------------------------- helpers ---
def rrect(d, box, r, fill=None, outline=None):
    d.rounded_rectangle(box, radius=r, fill=fill, outline=outline)


def text(d, xy, s, f, fill, anchor="la"):
    d.text(xy, s, font=f, fill=fill, anchor=anchor)


def badge(d, x, y, label, colour, w=None, h=22):
    """Route chip, e.g. '20' or 'E-W'. Returns its width."""
    f = BOLD(14)
    w = w or int(d.textlength(label, font=f) + 14)
    rrect(d, (x, y, x + w, y + h), 4, fill=colour)
    text(d, (x + w / 2, y + h / 2), label, f, (8, 12, 18), anchor="mm")
    return w


def live_dot(d, x, y, on=True):
    col = C["live"] if on else C["dim"]
    d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=col)


# ---------------------------------------------------------------- sprites ---
def bus_sprite(d, x, y, s=1, colour=None):
    """Chunky side-on bus, origin = bottom-left of the wheels. ~34x16 at s=1."""
    c = colour or C["bus"]
    rrect(d, (x, y - 15 * s, x + 34 * s, y - 3 * s), 3 * s, fill=c)
    for i in range(4):
        wx = x + (4 + i * 7) * s
        d.rectangle((wx, y - 12 * s, wx + 5 * s, y - 8 * s), fill=(210, 240, 255))
    d.rectangle((x + 32 * s, y - 8 * s, x + 34 * s, y - 6 * s), fill=C["warn"])
    for wx in (x + 7 * s, x + 26 * s):
        d.ellipse((wx - 3 * s, y - 5 * s, wx + 3 * s, y + s), fill=(20, 24, 30))


def train_sprite(d, x, y, s=1, colour=None):
    """Side-on two-car EMU. ~48x16 at s=1."""
    c = colour or C["train"]
    for cx in (x, x + 26 * s):
        rrect(d, (cx, y - 15 * s, cx + 22 * s, y - 3 * s), 3 * s, fill=c)
        for i in range(2):
            wx = cx + (4 + i * 8) * s
            d.rectangle((wx, y - 12 * s, wx + 6 * s, y - 8 * s), fill=(210, 240, 255))
        d.rectangle((cx + 2 * s, y - 5 * s, cx + 20 * s, y - 3 * s), fill=(20, 24, 30))
    d.rectangle((x + 46 * s, y - 8 * s, x + 48 * s, y - 6 * s), fill=C["warn"])


SPRITE = {"bus": bus_sprite, "train": train_sprite}
SPRITE_W = {"bus": 34, "train": 48}


# ------------------------------------------------------------- mock data ---
WATCHES = [
    dict(kind="bus", badge="20", where="Stop 8213", dest="to City Centre",
         mins=[4, 17], live=[True, False], colour=C["bus"]),
    dict(kind="train", badge="E-W", where="Kingsland", dest="to Waitemata",
         mins=[7, 22], live=[True, True], colour=C["train"]),
    dict(kind="bus", badge="24B", where="Stop 8213", dest="to New Lynn",
         mins=[11, 31], live=[True, False], colour=C["bus"]),
    dict(kind="train", badge="O-W", where="Kingsland", dest="to Onehunga",
         mins=[19, 49], live=[False, False], colour=C["train"]),
]

HORIZON = 20.0  # minutes of "approach lane" a vehicle travels across


def base(bg=None):
    img = Image.new("RGB", (W, H), bg or C["bg"])
    return img, ImageDraw.Draw(img)


def status_bar(d, y=0, h=18, clock="17:42"):
    d.rectangle((0, y, W, y + h), fill=C["panel"])
    text(d, (6, y + h / 2), "Sandringham", REG(11), C["dim"], anchor="lm")
    text(d, (W - 6, y + h / 2), clock, MONO(12), C["text"], anchor="rm")
    live_dot(d, W - 46, y + h / 2, True)
    text(d, (W - 54, y + h / 2), "live", REG(10), C["dim"], anchor="rm")


# ------------------------------------------------------------- layout A ----
def layout_a(watches, t=0.0):
    """Duo cards: two watches, large, each with its own approach lane."""
    img, d = base()
    ws = watches[:2]
    ch = H // len(ws)
    for i, w in enumerate(ws):
        y = i * ch
        rrect(d, (4, y + 4, W - 4, y + ch - 4), 6, fill=C["panel"])
        bw = badge(d, 12, y + 12, w["badge"], w["colour"])
        text(d, (12 + bw + 8, y + 14), w["where"], BOLD(13), C["text"])
        text(d, (12 + bw + 8, y + 30), w["dest"], REG(11), C["dim"])

        text(d, (W - 16, y + 34), str(w["mins"][0]), MONO(38), C["text"], anchor="rm")
        text(d, (W - 16, y + 58), "min", REG(10), C["dim"], anchor="rm")
        live_dot(d, W - 96, y + 34, w["live"][0])
        text(d, (W - 104, y + 34), "then " + str(w["mins"][1]), REG(11), C["dim"], anchor="rm")

        ly = y + ch - 16
        d.rectangle((12, ly, W - 12, ly + 3), fill=C["road"] if w["kind"] == "bus" else C["rail"])
        d.rectangle((W - 16, ly - 10, W - 12, ly + 3), fill=C["dim"])
        prog = 1.0 - min(w["mins"][0], HORIZON) / HORIZON
        sx = 12 + prog * (W - 28 - SPRITE_W[w["kind"]])
        SPRITE[w["kind"]](d, sx, ly + math.sin(t * 6 + i) * 1.2, 1, w["colour"])
    return img


# ------------------------------------------------------------- layout B ----
def layout_b(watches, t=0.0):
    """Status bar + compact rows (scales to 4) + shared animated scene strip."""
    img, d = base()
    status_bar(d)
    top, strip_h = 18, 58
    rh = (H - top - strip_h) // len(watches)
    for i, w in enumerate(watches):
        y = top + i * rh
        if i:
            d.line((8, y, W - 8, y), fill=C["line"])
        bw = badge(d, 8, y + (rh - 22) // 2, w["badge"], w["colour"])
        text(d, (8 + bw + 8, y + rh / 2 - 7), w["where"], BOLD(12), C["text"], anchor="lm")
        text(d, (8 + bw + 8, y + rh / 2 + 7), w["dest"], REG(10), C["dim"], anchor="lm")
        text(d, (W - 34, y + rh / 2), str(w["mins"][0]), MONO(24), C["text"], anchor="rm")
        text(d, (W - 6, y + rh / 2 + 3), "min", REG(9), C["dim"], anchor="rm")
        live_dot(d, W - 74, y + rh / 2, w["live"][0])
        text(d, (W - 82, y + rh / 2), str(w["mins"][1]), REG(11), C["dim"], anchor="rm")

    sy = H - strip_h
    d.rectangle((0, sy, W, H), fill=C["panel"])
    rail_y, road_y = sy + 24, H - 8
    d.rectangle((0, rail_y, W, rail_y + 2), fill=C["rail"])
    d.rectangle((0, road_y, W, road_y + 2), fill=C["road"])
    for x in range(int(-t * 40) % 24 - 24, W, 24):
        d.rectangle((x, road_y, x + 10, road_y + 1), fill=C["dim"])
    train_sprite(d, (t * 55) % (W + 60) - 60, rail_y, 1)
    bus_sprite(d, (t * 34) % (W + 40) - 40, road_y, 1)
    return img


# ------------------------------------------------------------- layout C ----
def layout_c(watches, t=0.0):
    """Approach lanes: one lane per watch, vehicle position = ETA. Scales 1-4."""
    img, d = base()
    status_bar(d)
    top = 18
    lh = (H - top) // len(watches)
    for i, w in enumerate(watches):
        y = top + i * lh
        rrect(d, (4, y + 3, W - 4, y + lh - 3), 5,
              fill=C["panel"] if i % 2 == 0 else C["panel_hi"])
        bw = badge(d, 10, y + 8, w["badge"], w["colour"], h=18)
        text(d, (10 + bw + 6, y + 10), w["dest"], REG(10), C["dim"])
        text(d, (W - 12, y + 6), str(w["mins"][0]), MONO(20), C["text"], anchor="ra")
        text(d, (W - 12, y + 28), "then " + str(w["mins"][1]), REG(9), C["dim"], anchor="ra")

        ly = y + lh - 12
        d.rectangle((10, ly, W - 58, ly + 2),
                    fill=C["road"] if w["kind"] == "bus" else C["rail"])
        d.rectangle((W - 60, ly - 12, W - 58, ly + 2), fill=C["dim"])
        d.ellipse((W - 63, ly - 17, W - 55, ly - 9), fill=w["colour"])
        prog = 1.0 - min(w["mins"][0], HORIZON) / HORIZON
        sx = 10 + prog * (W - 70 - SPRITE_W[w["kind"]])
        SPRITE[w["kind"]](d, sx, ly + math.sin(t * 5 + i * 1.7), 1, w["colour"])
    return img


LAYOUTS = {
    "A  Duo cards": (layout_a, 2),
    "B  List + scene strip": (layout_b, 4),
    "C  Approach lanes": (layout_c, 3),
}


def sheet(scale=2, t=0.0):
    """Contact sheet of every layout, labelled, at NEAREST upscale."""
    pad, lab = 12, 22
    cells = [(name, fn(WATCHES[:n], t)) for name, (fn, n) in LAYOUTS.items()]
    cw, ch = W * scale, H * scale
    out = Image.new("RGB", (pad + len(cells) * (cw + pad), pad + lab + ch + pad),
                    (18, 18, 20))
    d = ImageDraw.Draw(out)
    for i, (name, im) in enumerate(cells):
        x = pad + i * (cw + pad)
        out.paste(im.resize((cw, ch), Image.NEAREST), (x, pad + lab))
        d.text((x, pad + 4), name, font=BOLD(15), fill=(235, 235, 240))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-show", action="store_true")
    ap.add_argument("--gif", action="store_true")
    ap.add_argument("--scale", type=int, default=2)
    args = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    s = sheet(args.scale)
    p = os.path.join(OUT, "layouts.png")
    s.save(p)
    print("wrote", p)

    for name, (fn, n) in LAYOUTS.items():
        key = name.split()[0].lower()
        fn(WATCHES[:n], 0.0).resize((W * 3, H * 3), Image.NEAREST).save(
            os.path.join(OUT, "layout_" + key + ".png"))
        if args.gif:
            frames = [fn(WATCHES[:n], i / 12).resize((W * 2, H * 2), Image.NEAREST)
                      for i in range(48)]
            gp = os.path.join(OUT, "layout_" + key + ".gif")
            frames[0].save(gp, save_all=True, append_images=frames[1:],
                           duration=80, loop=0, optimize=True)
            print("wrote", gp)

    if not args.no_show:
        s.show()


if __name__ == "__main__":
    sys.exit(main())
