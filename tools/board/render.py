"""Compose model + layout + sprites into the 320x240 frame.

Mirrors what the firmware does per lane, so what you see here is what the
ILI9341 shows.
"""

import math
import os

from PIL import Image, ImageDraw, ImageFont

from . import layout, palette, sprites
from .model import Board

FONT_DIR = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")
_cache = {}

DIM_FACTOR = 0.45


def _font(name, size):
    key = (name, size)
    if key not in _cache:
        try:
            _cache[key] = ImageFont.truetype(os.path.join(FONT_DIR, name), size)
        except OSError:
            _cache[key] = ImageFont.load_default()
    return _cache[key]


def bold(s):
    return _font("arialbd.ttf", s)


def reg(s):
    return _font("arial.ttf", s)


def mono(s):
    return _font("consolab.ttf", s)


def _mins(eta_s):
    """Seconds to the number actually shown. Never shows a negative."""
    return max(0, int(eta_s) // 60)


def _status_bar(d, board):
    d.rectangle((0, 0, layout.W, layout.STATUS_H), fill=palette.BASE["panel"])
    mid = layout.STATUS_H / 2
    d.text((6, mid), "Kingsland", font=reg(11), fill=palette.BASE["dim"], anchor="lm")
    d.text((layout.W - 6, mid), board.clock, font=mono(12),
           fill=palette.BASE["text"], anchor="rm")

    if board.is_stale:
        label, colour = f"stale {board.stale_s // 60}m", palette.BASE["warn"]
    else:
        label, colour = "live", palette.BASE["live"]
    d.text((layout.W - 54, mid), label, font=reg(10),
           fill=palette.BASE["dim"], anchor="rm")
    dx = layout.W - 46
    d.ellipse((dx - 3, mid - 3, dx + 3, mid + 3), fill=colour)


def _lane(d, ln, watch, t, index):
    card = palette.BASE["panel"] if index % 2 == 0 else palette.BASE["panel_hi"]
    d.rounded_rectangle(
        (ln.rect.x0 + layout.MARGIN, ln.rect.y0 + 3,
         ln.rect.x1 - layout.MARGIN, ln.rect.y1 - 3),
        radius=5, fill=card)

    nxt = watch.next
    colour = watch.colour

    # route badge
    f = bold(13)
    label = watch.badge
    bw = int(d.textlength(label, font=f) + 14)
    b = ln.badge
    d.rounded_rectangle((b.x0, b.y0, b.x0 + bw, b.y1), radius=4, fill=colour)
    d.text(((b.x0 + b.x0 + bw) / 2, (b.y0 + b.y1) / 2), label, font=f,
           fill=palette.BASE["dark"], anchor="mm")

    d.text((b.x0 + bw + 8, ln.headsign_xy[1]), watch.headsign, font=reg(10),
           fill=palette.BASE["dim"])

    # times
    if nxt is None:
        d.text(ln.minutes_xy, "--", font=mono(20), fill=palette.BASE["dim"], anchor="ra")
        d.text(ln.following_xy, "none tonight", font=reg(9),
               fill=palette.BASE["dim"], anchor="ra")
    else:
        txt_colour = palette.BASE["dim"] if nxt.cancelled else palette.BASE["text"]
        d.text(ln.minutes_xy, str(_mins(nxt.eta_s)), font=mono(20),
               fill=txt_colour, anchor="ra")
        if nxt.cancelled:
            box = d.textbbox(ln.minutes_xy, str(_mins(nxt.eta_s)),
                             font=mono(20), anchor="ra")
            y = (box[1] + box[3]) // 2
            d.line((box[0] - 2, y, box[2] + 2, y), fill=palette.BASE["warn"], width=2)
        following = watch.following
        d.text(ln.following_xy,
               f"then {_mins(following.eta_s)}" if following else "then --",
               font=reg(9), fill=palette.BASE["dim"], anchor="ra")

    # track + stop marker
    track_colour = palette.BASE["road"] if watch.kind == "bus" else palette.BASE["rail"]
    d.rectangle(ln.track, fill=track_colour)
    d.rectangle((ln.marker_x, ln.track.y0 - 12, ln.marker_x + 2, ln.track.y1),
                fill=palette.BASE["dim"])
    d.ellipse((ln.marker_x - 3, ln.track.y0 - 17, ln.marker_x + 5, ln.track.y0 - 9),
              fill=colour)

    if nxt is None:
        # Parked at the left, lights off.
        sprites.blit(d, watch.kind, ln.track.x0, ln.sprite_baseline,
                     palette.shade(colour, 0.4))
        return

    x = layout.vehicle_x(nxt.eta_s, ln, watch.kind)
    bob = math.sin(t * 5 + index * 1.7) if nxt.eta_s > 30 else 0
    body = palette.shade(colour, 0.4) if nxt.cancelled else colour
    sprites.blit(d, watch.kind, x, ln.sprite_baseline + bob, body)


def render(board: Board, t: float = 0.0) -> Image.Image:
    img = Image.new("RGB", (layout.W, layout.H), palette.BASE["bg"])
    d = ImageDraw.Draw(img)
    _status_bar(d, board)
    n = len(board.watches)
    for i, watch in enumerate(board.watches):
        _lane(d, layout.lane(i, n), watch, t, i)
    if board.dimmed:
        img = Image.eval(img, lambda v: int(v * DIM_FACTOR))
    return img
