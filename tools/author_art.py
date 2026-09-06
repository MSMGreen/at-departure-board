"""Parametric source for every vehicle sprite.

The art is DRAWN here, not typed as rows - hand-counted pixel grids are how
you get a 46-wide sprite declared as 48. Run this to regenerate
tools/board/art/sprites_data.py after changing any shape.

    python tools/author_art.py            # regenerate the data module
    python tools/author_art.py --preview  # also write a preview sheet

Large  (1-2 lanes, 111px+): ~72-92 wide, 38-40 tall
Compact (3-4 lanes, 74/55px): ~36-48 wide, 18 tall
"""
import math
import os
import sys

ROLES = ".BHSMWGDKLA"
#  . transparent  B body  H highlight  S shade  M midshadow  W window
#  G glint  D outline  K detail/dark  L lamp  A accent


class G:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.g = [["."] * w for _ in range(h)]

    def px(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.g[y][x] = c
        return self

    def rect(self, x0, y0, x1, y1, c):
        for y in range(max(0, y0), min(self.h, y1 + 1)):
            for x in range(max(0, x0), min(self.w, x1 + 1)):
                self.g[y][x] = c
        return self

    def rrect(self, x0, y0, x1, y1, r, c):
        for y in range(max(0, y0), min(self.h, y1 + 1)):
            for x in range(max(0, x0), min(self.w, x1 + 1)):
                dx = max(x0 + r - x, x - (x1 - r), 0)
                dy = max(y0 + r - y, y - (y1 - r), 0)
                if dx * dx + dy * dy <= r * r + r * 0.7:
                    self.g[y][x] = c
        return self

    def ell(self, cx, cy, rx, ry, c):
        for y in range(int(cy - ry), int(cy + ry) + 1):
            for x in range(int(cx - rx), int(cx + rx) + 1):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                    self.px(x, y, c)
        return self

    def outline(self, src="B", oc="D"):
        snap = [r[:] for r in self.g]
        for y in range(self.h):
            for x in range(self.w):
                if snap[y][x] != ".":
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < self.w and 0 <= ny < self.h and snap[ny][nx] == src:
                        self.g[y][x] = oc
                        break
        return self

    def toplight(self, band=2, c="H"):
        for x in range(self.w):
            for y in range(self.h):
                if self.g[y][x] == "B":
                    for k in range(band):
                        if y + k < self.h and self.g[y + k][x] == "B":
                            self.g[y + k][x] = c
                    break
        return self

    def botshade(self, band=3, c="M"):
        for x in range(self.w):
            for y in range(self.h - 1, -1, -1):
                if self.g[y][x] == "B":
                    for k in range(band):
                        if y - k >= 0 and self.g[y - k][x] == "B":
                            self.g[y - k][x] = c
                    break
        return self

    def done(self):
        rows = ["".join(r) for r in self.g]
        assert all(len(r) == self.w for r in rows), "width"
        bad = {c for r in rows for c in r} - set(ROLES)
        assert not bad, f"unknown roles {bad}"
        return rows


ART = {}

# ======================================================= GHIBLI - LARGE =====
g = G(76, 40)
g.rrect(3, 6, 71, 28, 9, "B")
g.ell(58, 15, 16, 11, "B")
for ex in (46, 62):
    for i in range(9):
        g.rect(ex + i, i, ex + 8 - i, 8, "B")
g.ell(6, 16, 5, 9, "B")
g.rect(0, 11, 6, 15, "B")
g.toplight(2); g.botshade(3)
for wx in (10, 22, 34):
    g.rrect(wx, 11, wx + 9, 20, 2, "W")
    g.rect(wx + 1, 12, wx + 3, 13, "G")
for ex in (50, 63):
    g.ell(ex, 14, 6, 6, "W")
    g.rect(ex - 1, 10, ex + 1, 18, "K")
    g.ell(ex, 14, 2, 5, "K")
    g.rect(ex - 3, 11, ex - 2, 12, "G")
g.rect(46, 22, 70, 23, "K")
for tx in range(48, 70, 5):
    g.rect(tx, 22, tx + 1, 24, "W")
g.ell(72, 18, 3, 3, "L")
g.rect(70, 12, 75, 12, "K"); g.rect(70, 16, 75, 16, "K")
for lx in range(6, 66, 8):
    g.rect(lx, 28, lx + 4, 34, "S")
    g.rect(lx, 34, lx + 4, 36, "K")
g.outline()
ART[("ghibli", "large", "bus")] = g.done()

g = G(92, 40)
for cx in (0, 48):
    g.rrect(cx + 1, 5, cx + 42, 28, 5, "B")
    g.rect(cx + 2, 3, cx + 41, 6, "A")
    for wx in range(cx + 5, cx + 40, 11):
        g.rrect(wx, 9, wx + 8, 22, 2, "L")
        g.rect(wx + 1, 11, wx + 2, 13, "G")
    g.rect(cx + 1, 26, cx + 42, 28, "M")
    for bx in (cx + 7, cx + 30):
        g.rect(bx, 29, bx + 9, 33, "S")
        g.ell(bx + 2, 34, 3, 3, "K"); g.ell(bx + 7, 34, 3, 3, "K")
g.rrect(70, 8, 78, 24, 3, "K")
g.ell(74, 8, 4, 4, "K")
g.rect(72, 7, 76, 8, "W")
g.px(72, 9, "W"); g.px(76, 9, "W")
g.ell(90, 16, 3, 4, "L")
g.toplight(1)
g.outline()
ART[("ghibli", "large", "train")] = g.done()

# ===================================================== GHIBLI - COMPACT =====
g = G(38, 18)
g.rrect(1, 3, 35, 12, 4, "B")
g.ell(28, 7, 8, 5, "B")
for ex in (23, 30):
    g.rect(ex, 0, ex + 3, 3, "B")
g.toplight(1); g.botshade(1)
for wx in (4, 11):
    g.rect(wx, 5, wx + 4, 9, "W")
for ex in (25, 31):
    g.ell(ex, 6, 3, 3, "W")
    g.rect(ex, 4, ex, 8, "K")
g.rect(22, 10, 34, 10, "K")
g.ell(36, 8, 1, 1, "L")
for lx in range(3, 33, 5):
    g.rect(lx, 13, lx + 2, 16, "S")
g.outline()
ART[("ghibli", "compact", "bus")] = g.done()

g = G(48, 18)
for cx in (0, 25):
    g.rrect(cx, 3, cx + 21, 12, 3, "B")
    g.rect(cx + 1, 2, cx + 20, 4, "A")
    for wx in range(cx + 3, cx + 20, 6):
        g.rect(wx, 6, wx + 3, 10, "L")
    for bx in (cx + 4, cx + 15):
        g.rect(bx, 13, bx + 4, 15, "S")
g.rect(34, 5, 38, 12, "K")
g.rect(35, 4, 37, 5, "W")
g.rect(46, 7, 47, 8, "L")
g.outline()
ART[("ghibli", "compact", "train")] = g.done()

# ====================================================== TRANSIT - LARGE =====
g = G(72, 38)
g.rrect(2, 4, 69, 27, 4, "B")
g.rect(3, 4, 68, 8, "A")                       # destination blind band
g.toplight(2); g.botshade(3)
for wx in (6, 19, 32):                         # saloon windows
    g.rrect(wx, 11, wx + 10, 21, 2, "W")
    g.rect(wx + 1, 12, wx + 3, 13, "G")
g.rrect(47, 10, 66, 22, 3, "W")                # windscreen
g.rect(48, 11, 52, 13, "G")
g.rect(45, 10, 46, 24, "K")                    # door line
g.rect(66, 17, 69, 21, "L")                    # headlight
g.rect(3, 18, 5, 21, "L")
g.rect(2, 25, 69, 27, "M")
for wx in (12, 50):                            # wheels
    g.ell(wx + 4, 30, 7, 6, "K")
    g.ell(wx + 4, 30, 4, 3, "S")
g.rect(2, 33, 69, 34, "K")                     # shadow
g.outline()
ART[("transit", "large", "bus")] = g.done()

g = G(88, 38)
for cx in (0, 46):
    g.rrect(cx + 1, 4, cx + 40, 27, 4, "B")
    g.rect(cx + 2, 4, cx + 39, 7, "A")
    for wx in range(cx + 5, cx + 36, 11):
        g.rrect(wx, 11, wx + 8, 21, 2, "W")
        g.rect(wx + 1, 12, wx + 2, 13, "G")
    g.rect(cx + 1, 25, cx + 40, 27, "M")
    for bx in (cx + 6, cx + 28):
        g.rect(bx, 28, bx + 10, 32, "S")
        g.ell(bx + 3, 33, 3, 3, "K"); g.ell(bx + 8, 33, 3, 3, "K")
g.toplight(2)
g.rect(84, 6, 87, 24, "L")                     # yellow cab end
g.rect(80, 16, 84, 20, "L")
g.outline()
ART[("transit", "large", "train")] = g.done()

# ==================================================== TRANSIT - COMPACT =====
g = G(36, 16)
g.rrect(1, 2, 34, 11, 2, "B")
g.rect(2, 2, 33, 4, "A")
g.toplight(1); g.botshade(1)
for wx in (4, 12, 20):
    g.rect(wx, 6, wx + 5, 9, "W")
g.rect(27, 5, 32, 10, "W")
g.rect(33, 8, 34, 10, "L")
for wx in (6, 25):
    g.ell(wx + 2, 13, 3, 3, "K")
g.outline()
ART[("transit", "compact", "bus")] = g.done()

g = G(48, 16)
for cx in (0, 25):
    g.rrect(cx, 2, cx + 21, 11, 2, "B")
    g.rect(cx + 1, 2, cx + 20, 4, "A")
    for wx in range(cx + 3, cx + 19, 6):
        g.rect(wx, 6, wx + 3, 9, "W")
    for bx in (cx + 4, cx + 15):
        g.rect(bx, 12, bx + 4, 14, "S")
g.toplight(1)
g.rect(45, 4, 47, 10, "L")
g.outline()
ART[("transit", "compact", "train")] = g.done()

# =========================================================== validate =======
CAPS = {"large": 44, "compact": 18}
for (theme, size, kind), rows in ART.items():
    h, w = len(rows), len(rows[0])
    assert h <= CAPS[size], f"{theme}/{size}/{kind} too tall {h}"
    assert all(len(r) == w for r in rows), f"{theme}/{size}/{kind} ragged"
    xs = [x for r in rows for x, c in enumerate(r) if c == "L"]
    assert xs and max(xs) >= w - 4, f"{theme}/{size}/{kind} no leading light"
    assert w <= 100, f"{theme}/{size}/{kind} too wide {w}"
print(f"validated {len(ART)} sprites")
for k in sorted(ART):
    print("   ", "/".join(k), f"{len(ART[k][0])}x{len(ART[k])}")


# ============================================================== emit ========
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tools", "board", "art", "sprites_data.py")

NL = chr(10)


def render_module():
    lines = [
        '"""Generated by tools/author_art.py - do not edit by hand."""',
        "",
        "SPRITES = {",
    ]
    for key in sorted(ART):
        theme, size, kind = key
        rows = ART[key]
        lines.append(f'    ("{theme}", "{size}", "{kind}"): (')
        lines.append(f"        {len(rows[0])}, {len(rows)}, [")
        for r in rows:
            lines.append(f'            "{r}",')
        lines.append("        ]),")
    lines += ["}", ""]
    return NL.join(lines)


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline=NL) as fh:
        fh.write(render_module())
    print("wrote", os.path.normpath(OUT), f"({os.path.getsize(OUT)} bytes)")


if __name__ == "__main__":
    main()
