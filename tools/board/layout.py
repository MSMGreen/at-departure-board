"""Pure geometry for the approach-lane layout. No drawing happens here.

The one idea worth stating: a vehicle's x position IS its time to arrival.
It enters at the left at HORIZON_S and touches the stop marker at zero, so the
board can be read from across a room without resolving any digits.
"""

from typing import List, NamedTuple



W, H = 320, 240
STATUS_H = 18
HORIZON_S = 20 * 60

MARGIN = 4          # gap from screen edge to lane card
PAD = 6             # gap from card edge to content
MARKER_INSET = 58   # distance from right edge to the stop marker
TRACK_LIFT = 12     # track height above the card's bottom edge


class Rect(NamedTuple):
    x0: int
    y0: int
    x1: int
    y1: int

    @property
    def width(self):
        return self.x1 - self.x0

    @property
    def height(self):
        return self.y1 - self.y0


class Lane(NamedTuple):
    rect: Rect
    badge: Rect
    headsign_xy: tuple
    minutes_xy: tuple
    following_xy: tuple
    track: Rect
    marker_x: int
    sprite_baseline: int


def lane_rects(n) -> List[Rect]:
    if not 1 <= n <= 4:
        raise ValueError(f"board supports 1..4 lanes, got {n}")
    avail = H - STATUS_H
    lh = avail // n
    return [Rect(0, STATUS_H + i * lh, W, STATUS_H + (i + 1) * lh) for i in range(n)]


def lane(index, n) -> Lane:
    r = lane_rects(n)[index]
    badge_h = 18
    badge = Rect(r.x0 + 10, r.y0 + PAD, r.x0 + 10 + 34, r.y0 + PAD + badge_h)

    track_y = r.y1 - TRACK_LIFT
    marker_x = W - MARKER_INSET
    track = Rect(r.x0 + 10, track_y, marker_x, track_y + 2)

    return Lane(
        rect=r,
        badge=badge,
        headsign_xy=(badge.x1 + 8, r.y0 + PAD + 2),
        minutes_xy=(W - 12, r.y0 + PAD),
        following_xy=(W - 12, r.y0 + PAD + 22),
        track=track,
        marker_x=marker_x,
        sprite_baseline=track_y,
    )


def vehicle_x(eta_s, lane, sprite_w):
    """Map seconds-until-arrival to an x position on the lane's track.

    HORIZON_S or more -> the left end. Zero or less -> touching the marker.

    Takes a width rather than a kind because the width now depends on the
    theme and the size class, which layout has no business knowing about.
    """
    sw = sprite_w
    x_start = lane.track.x0
    x_stop = lane.marker_x - sw
    clamped = max(0, min(int(eta_s), HORIZON_S))
    progress = 1.0 - clamped / HORIZON_S
    return int(round(x_start + progress * (x_stop - x_start)))
