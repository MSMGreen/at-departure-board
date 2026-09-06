"""A theme is data: colours, sprites, what each role means, and a scene."""

from dataclasses import dataclass
from typing import Callable, Dict, Optional, Tuple

from .. import palette
from ..sprites import Sprite


@dataclass(frozen=True)
class Theme:
    name: str
    label: str
    colours: Dict[str, tuple]
    sprites: Dict[tuple, Sprite]          # (size, kind) -> Sprite
    kind_fallback: Dict[str, tuple]
    roles: Dict[str, object]              # tuple, or "shade"/"bright"/"body"
    use_route_color: bool = False
    scenery: Optional[Callable] = None    # (draw, rect, kind, seed) -> None

    def colour(self, key) -> Tuple[int, int, int]:
        try:
            return self.colours[key]
        except KeyError:
            raise KeyError(f"theme {self.name!r} has no colour {key!r}") from None

    def badge_colour(self, kind, route_color=None):
        if self.use_route_color:
            parsed = palette.parse_hex(route_color)
            if parsed:
                return parsed
        return self.kind_fallback.get(kind, self.colour("dim"))

    def role_colours(self, body):
        out = {"B": body}
        for role, value in self.roles.items():
            if value == "body":
                out[role] = body
            elif value == "shade":
                out[role] = palette.shade(body)
            elif value == "bright":
                out[role] = palette.bright(body)
            else:
                out[role] = value
        return out

    def sprite(self, size, kind) -> Sprite:
        try:
            return self.sprites[(size, kind)]
        except KeyError:
            raise KeyError(
                f"theme {self.name!r} has no {size}/{kind} sprite") from None
