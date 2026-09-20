"""The enclosure model: does it still clear everything it is meant to?

The point of these is not to re-check OpenSCAD's arithmetic. It is that
`models/src/wedge.scad` is a wall of constants, and editing one in isolation can
silently close a gap somewhere else. Each clearance the design depends on is
asserted here against the constants actually in the file, so a change that eats
one fails loudly.

Design: docs/design/specs/2026-09-20-enclosure-wedge-design.md
"""

import math
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
SCAD = ROOT / "models" / "src" / "wedge.scad"

OPENSCAD_CANDIDATES = [
    "openscad",
    r"C:\Program Files\OpenSCAD\openscad.exe",
    r"C:\Program Files (x86)\OpenSCAD\openscad.exe",
    "/usr/bin/openscad",
    "/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD",
]


def find_openscad():
    for c in OPENSCAD_CANDIDATES:
        if shutil.which(c) or Path(c).exists():
            return c
    return None


@pytest.fixture(scope="module")
def k():
    """Top-level `name = number;` constants, read straight from the model."""
    text = SCAD.read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r"^(\w+)\s*=\s*(-?[\d.]+)\s*;", text, re.M):
        out[m.group(1)] = float(m.group(2))
    # derived, mirroring the model
    out["face_len"] = out["case_h"] / math.cos(math.radians(out["rake"]))
    out["floor_z"] = out["wall"]
    out["dk_z"] = out["floor_z"] + out["dk_hous"]
    out["dk_y1"] = out["case_d"] - out["cov_t"] - out["dk_back"]
    out["dk_y0"] = out["dk_y1"] - out["dk_w"]
    out["pcb_v0"] = (out["face_len"] - out["pcb_w"]) / 2
    return out


def _y_at(z, rake):
    """Depth of the raked front face at height z."""
    return z * math.tan(math.radians(rake))


# --------------------------------------------------------------- the panel


def test_aperture_clears_the_active_area(k):
    """The bezel must not eat pixels, with the tolerance stack spent."""
    stack = 0.2 + 0.2  # pocket clearance + PCB outline tolerance
    assert k["ap_m"] - stack >= 1.0, "aperture margin no longer absorbs the stack"


def test_aperture_stays_inside_the_glass(k):
    """Drift the other way must show glass, not a gap past the PCB edge."""
    glass_l, glass_w = 69.20, 50.00
    ap_l = k["aa_l"] + 2 * k["ap_m"]
    ap_w = k["aa_w"] + 2 * k["ap_m"]
    assert (glass_l - ap_l) / 2 > 0.4, "aperture reaches the glass edge lengthwise"
    assert (glass_w - ap_w) / 2 > 0.4, "aperture reaches the glass edge widthwise"


def test_locating_pins_miss_the_glass(k):
    """Pins sit in the bare strips either side of the glass, not under it."""
    bare_end = (k["pcb_l"] - 69.20) / 2  # 6.25
    assert k["hole_end"] + k["hole_d"] / 2 < bare_end, "pin column fouls the glass"


def test_pins_do_not_protrude_through_the_panel(k):
    assert k["pin_h"] < k["pcb_t"], "locating pin stands proud of the PCB"


def test_face_thickness_sets_the_glass_flush(k):
    """face_t is the glass's proud height; drifting apart recesses the screen."""
    glass_proud = 3.6
    assert abs(k["face_t"] - glass_proud) < 0.01


# -------------------------------------------------------------- the DevKit


def test_devkit_bay_clears_the_sd_socket(k):
    """The tightest point in the whole case: top of the DevKit stack.

    The SD socket sits 7.8 behind the glass face and against a *long* edge of the
    panel, so it may sit directly over the bay. This is why case_d is 48.
    """
    z_top = k["dk_z"] + k["dk_pcb_t"] + 3.2  # top of the chip
    sd_back = _y_at(z_top, k["rake"]) + 7.8 * math.cos(math.radians(k["rake"]))
    assert k["dk_y0"] - sd_back > 2.0, (
        f"DevKit front {k['dk_y0']:.2f} vs SD back {sd_back:.2f} "
        "- raise case_d or move the bay back"
    )


def test_devkit_bay_clears_the_panel_back(k):
    z_top = k["dk_z"] + k["dk_pcb_t"] + 3.2
    pcb_back = _y_at(z_top, k["rake"]) + 5.2 * math.cos(math.radians(k["rake"]))
    assert k["dk_y0"] - pcb_back > 2.0


def test_devkit_fits_between_floor_and_panel(k):
    """Housing well + PCB + chip must fit under the panel's lower edge."""
    stack = k["dk_hous"] + k["dk_pcb_t"] + 3.2
    assert k["floor_z"] + stack < k["case_h"] - k["wall"]


def test_devkit_sits_clear_of_the_cover(k):
    assert k["dk_y1"] <= k["case_d"] - k["cov_t"]


def test_usb_opening_lands_on_the_connector(k):
    """Opening centred on the USB-C body, which sits on the board's top face."""
    conn_lo = k["dk_z"] + k["dk_pcb_t"]
    conn_hi = conn_lo + 3.2
    op_lo = conn_lo + 1.6 - k["usb_h"] / 2
    op_hi = op_lo + k["usb_h"]
    assert op_lo < conn_lo and op_hi > conn_hi, "opening misses the connector"
    assert op_lo > k["floor_z"], "USB opening breaks into the floor"


# ---------------------------------------------------------------- the panel bay


def test_panel_housings_clear_the_back(k):
    """The panel's own jumper housings, 20.9 behind its glass, at the top."""
    rake = math.radians(k["rake"])
    v_top = k["pcb_v0"] + k["pcb_w"]
    z_top = v_top * math.cos(rake)
    back = _y_at(z_top, k["rake"]) + 20.9 * math.cos(rake)
    assert (k["case_d"] - k["cov_t"]) - back > 3.0, (
        f"panel housings reach y={back:.2f} against a cover at "
        f"{k['case_d'] - k['cov_t']:.2f}"
    )


def test_panel_fits_the_face(k):
    """Pocket plus rim must not run off the ends of the raked face."""
    pocket_w = k["pcb_w"] + k["pcb_gap"]
    assert pocket_w + 2 * k["rim_w"] < k["face_len"]
    assert k["pcb_l"] + k["pcb_gap"] + 2 * k["rim_w"] < k["case_w"]


# ------------------------------------------------------------------- render


@pytest.mark.skipif(find_openscad() is None, reason="OpenSCAD not installed")
@pytest.mark.parametrize("part", ["body", "cover"])
def test_part_renders_as_one_manifold_solid(part, tmp_path):
    """A stray feature that only *touches* the shell prints as loose plastic."""
    out = tmp_path / f"{part}.stl"
    proc = subprocess.run(
        [find_openscad(), "-o", str(out), "-D", f'part="{part}"', str(SCAD)],
        capture_output=True,
        text=True,
    )
    assert proc.returncode == 0, proc.stderr
    assert out.exists() and out.stat().st_size > 0
    assert "Simple:        yes" in proc.stderr, "mesh is not simple"
    volumes = re.search(r"Volumes:\s+(\d+)", proc.stderr)
    assert volumes and int(volumes.group(1)) == 2, (
        f"{part} is in {volumes.group(1) if volumes else '?'} pieces - an internal "
        "feature is touching the shell rather than fused into it"
    )


# ------------------------------------------------------------ panel retention


def test_snap_tabs_clear_the_panel_back(k):
    """The tab must sit behind the PCB, not clamp onto it or crush it."""
    gap = k["tab_gap"]
    assert 0.05 <= gap <= 0.4, "tab clearance over the PCB is out of range"


def test_snap_tabs_hide_behind_the_bezel(k):
    """A tab poking into the aperture would be visible on the finished board."""
    pocket_w = k["pcb_w"] + k["pcb_gap"]
    pocket_v0 = (k["face_len"] - pocket_w) / 2
    ap_v0 = (k["face_len"] - (k["aa_w"] + 2 * k["ap_m"])) / 2
    tab_reach = pocket_v0 + k["tab_lip"]
    assert tab_reach < ap_v0, (
        f"snap tab reaches v={tab_reach:.2f} but the aperture starts at "
        f"v={ap_v0:.2f} - it would show through the window"
    )


def test_snap_tabs_actually_grip(k):
    """Lip must overhang enough to hold, given the pocket's side clearance."""
    assert k["tab_lip"] > k["pcb_gap"] * 2, "lip is within the pocket slop"
