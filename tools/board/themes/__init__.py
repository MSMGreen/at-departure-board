"""Theme registry. Adding a theme means adding a module and one import."""

from .base import Theme  # noqa: F401
from . import ghibli, transit

DEFAULT = "transit"

_ALL = [transit.THEME, ghibli.THEME]
THEMES = {t.name: t for t in _ALL}


def names():
    return list(THEMES)


def get(name):
    try:
        return THEMES[name]
    except KeyError:
        raise KeyError(
            f"unknown theme {name!r}; available: {', '.join(sorted(THEMES))}"
        ) from None


def all_themes():
    return [THEMES[n] for n in sorted(THEMES)]
