#pragma once

// Terrain profiles for the Ghibli scenery: height in pixels at dx from the
// lane's left edge. Ports of scenery._hill and scenery._shore.
int hill(int dx);
int shore(int dx);

// dx ranges over the lane's scenery columns, never beyond the screen width.
constexpr int TERRAIN_DX_MAX = 320;

// hill(dx) / shore(dx) for 0 <= dx < TERRAIN_DX_MAX, computed once on first
// use. The panel's FPU is single precision, so hill()'s double sin() calls are
// software-emulated; the renderer redraws each lane once per band, and calling
// them per column per band cost ~40 ms a frame (docs/hardware-notes.md).
// Out-of-range dx falls back to the direct computation.
int hill_at(int dx);
int shore_at(int dx);
