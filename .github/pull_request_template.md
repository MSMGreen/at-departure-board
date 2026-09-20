## What this changes



## How it was tested

<!-- Delete what does not apply. "Renderer only, no hardware" is a complete
     answer - most of this project runs on a PC. -->

- [ ] `python -m pytest`
- [ ] `pio test -e native`
- [ ] `pio run -e esp32` builds
- [ ] Flashed and watched on real hardware

## Checklist

- [ ] Generated files regenerated and committed alongside the change
      (`tests/golden/` via `tools/regolden.py`, `src/sprites.h` via
      `tools/export_sprites.py`)
- [ ] Wiring tables in `README.md` and `docs/SETUP.md` still match, if pins moved
- [ ] No `src/secrets.h`, API key or WiFi password in the diff
