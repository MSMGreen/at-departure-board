# The enclosure

A raked wedge, 90 × 48 × 70 mm, in two printed parts: a body that holds the
panel and the DevKit, and a cover that snaps on the back.

```
src/wedge.scad          the model - every dimension is a named constant
build/wedge_body.stl    ready to slice
build/wedge_cover.stl   ready to slice
```

**To print one, take the two STLs in `build/`.** You do not need OpenSCAD for
that — they are committed for the same reason `src/sprites.h` is, so that
building the thing doesn't require the tool that generated it.

Print the body **on its back face**. The raked front then becomes a 15° overhang
that needs no support, and the face you look at comes out clean.

## To change it

`src/wedge.scad` is the source of truth. Case size, rake, wall thickness, panel
pocket, DevKit bay and every clearance are named constants at the top of the
file; change one and re-render:

```bash
openscad -o build/wedge_body.stl  -D 'part="body"'  src/wedge.scad
openscad -o build/wedge_cover.stl -D 'part="cover"' src/wedge.scad
```

Re-render and commit both STLs in the same commit as the `.scad` change, the
same rule the golden renders follow.

`python -m pytest tests/test_models.py` re-checks the clearances the design
depends on against the constants actually in the file, and that each part is
still a single manifold solid. It skips itself if OpenSCAD isn't installed.

## Where the numbers came from

[`../docs/enclosure.md`](../docs/enclosure.md) — every dimension of the case,
the panel and the DevKit, and the source of each (datasheet, measured, or
derived). [`../docs/assembly.md`](../docs/assembly.md) is how it goes together.
The reasoning behind the shape is in
[`../docs/design/specs/2026-09-20-enclosure-wedge-design.md`](../docs/design/specs/2026-09-20-enclosure-wedge-design.md).
