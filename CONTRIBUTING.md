# Contributing

Issues and pull requests are welcome — including "I built one and this bit of
the setup guide was wrong", which is the most useful report this project can
get.

## Getting set up

```bash
python -m pip install -r requirements-dev.txt
python -m pytest        # the renderer, the model, the tools
pio test -e native      # the firmware logic, compiled for your PC
```

You do not need the hardware to work on most of this. The whole renderer, the
board model, the themes and the layout run on your PC:

```bash
python tools/simulate.py --all --show
```

Firmware logic lives in `lib/core/src/` on purpose: no Arduino headers, so it
compiles and runs under `pio test -e native`. Anything that touches WiFi, NVS,
the panel or the web server lives in `src/` instead. If you're adding logic,
put it in `lib/core/` and test it there.

`pio test -e native` needs a host C++ compiler. If you don't have one, say so
in the PR — CI runs both suites on every push and pull request.

## The golden renders

`tests/golden/` holds byte-exact expected renders. A failing golden test means
the output changed.

- **Didn't mean to change the render?** That's a real failure. Fix it.
- **Meant to?** `python tools/regolden.py`, eyeball the new PNGs, and commit
  them in the same commit as the change that caused them. A golden update on
  its own, or a render change without one, is very hard to review.

## Generated files

Some committed files are generated. Edit the source, then regenerate:

| Generated | Source | Command |
|---|---|---|
| `src/sprites.h` | `tools/board/sprites.py` | `python tools/export_sprites.py` |
| the vehicle art | `tools/author_art.py` | `python tools/author_art.py` |
| `tests/golden/*.png` | the renderer | `python tools/regolden.py` |
| `models/build/*.stl` | `models/src/wedge.scad` | `openscad -o models/build/wedge_body.stl -D 'part="body"' models/src/wedge.scad` (and `part="cover"`) |

Art is never typed by hand — `tools/author_art.py` draws it parametrically, and
that's the file to edit.

## Before touching the network code

Read [docs/at-api-notes.md](docs/at-api-notes.md). It records how AT's API
actually behaves against live endpoints, and it differs from AT's own
documentation in five places. At least one of those differences, taken at face
value, makes the board re-resolve every stop every night.

Network responses are exercised against captured fixtures in `test/fixtures/`
rather than the live API, so tests stay offline and deterministic. New
behaviour wants a new fixture.

## Hardware changes

Pin assignments are build flags in `platformio.ini`, not code. If you change
one, change the wiring table in `README.md` and `docs/SETUP.md` in the same
commit — a wiring table that disagrees with the firmware is worse than none.

If you get the board running on different hardware (an S3, a different panel, a
bigger screen), that's worth a PR to the setup guide even if no code changes.

## Pull requests

- One concern per PR, with the generated files it implies.
- Say what you ran. "pytest green, `pio test -e native` green, flashed it and
  watched it for an hour" is a complete story; so is "renderer only, no
  hardware".
- Comments explain *why*, not what. The existing code is fairly heavily
  commented in that style — match it.
- `src/secrets.h` is gitignored. Check you haven't committed one:
  `git status --short` before you push.

## Security

Don't open a public issue for a security problem in the firmware — see
[SECURITY.md](SECURITY.md).

By contributing you agree your contributions are licensed under the MIT
licence, same as the rest of the project.
