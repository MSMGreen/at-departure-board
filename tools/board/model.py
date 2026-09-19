"""What the screen is showing, independent of how it is drawn.

This mirrors the struct the firmware builds after merging schedule with
realtime, so the simulator and the device agree on vocabulary.
"""

from dataclasses import dataclass, field
from typing import List, Optional

STALE_AFTER_S = 90
MAX_WATCHES = 4


@dataclass
class Departure:
    eta_s: int
    live: bool = False
    cancelled: bool = False
    scheduled: str = ""

    @property
    def is_departed(self):
        return self.eta_s < 0


@dataclass
class Watch:
    badge: str
    headsign: str
    kind: str
    departures: List[Departure] = field(default_factory=list)
    route_color: Optional[str] = None
    # Set when the board cannot honestly show times for this watch (spec 8).
    # A watch with a message shows no departures at all.
    message: Optional[str] = None

    def __post_init__(self):
        self.departures = sorted(self.departures, key=lambda d: d.eta_s)

    @property
    def next(self):
        return self.departures[0] if self.departures else None

    @property
    def following(self):
        return self.departures[1] if len(self.departures) > 1 else None


@dataclass
class Board:
    watches: List[Watch]
    clock: str
    stale_s: int = 0
    dimmed: bool = False
    theme: str = "transit"
    location: str = "Kingsland"

    def __post_init__(self):
        if not 1 <= len(self.watches) <= MAX_WATCHES:
            raise ValueError(
                f"board needs 1..{MAX_WATCHES} watches, got {len(self.watches)}"
            )

    @property
    def is_stale(self):
        return self.stale_s > STALE_AFTER_S
