// AT departure board - raked wedge enclosure
// Design: docs/design/specs/2026-09-20-enclosure-wedge-design.md
//
// Render:
//   openscad -o models/build/wedge_body.stl  -D part=\"body\"  models/src/wedge.scad
//   openscad -o models/build/wedge_cover.stl -D part=\"cover\" models/src/wedge.scad
//
// Every dimension below is either measured or taken from the panel datasheet.
// Adjust a constant and re-render; nothing is hard-coded further down.

part = "both";          // "body" | "cover" | "both"
$fn = 64;

// ---------------------------------------------------------------- case shell
case_w   = 90;          // width
case_d   = 48;          // depth, set by the DevKit bay clearing the SD socket
case_h   = 70;          // height
wall     = 2.5;
rake     = 15;          // front face, degrees from vertical
face_t   = 3.6;         // = glass proud height, so the glass finishes flush

// ---------------------------------------------------------------- the panel
pcb_l    = 81.70;       // datasheet, long axis (horizontal in landscape)
pcb_w    = 50.00;       // short axis
pcb_t    = 1.60;
pcb_gap  = 0.40;        // total pocket clearance

aa_l     = 57.60;       // active area
aa_w     = 43.20;
aa_off   = 0;           // active-area centre vs PCB centre, + toward the pin end.
                        // 0 = the datasheet's centred reading. Set 5.80 if a
                        // print shows the image off toward the SD end.
ap_m     = 1.70;        // aperture margin round the image (see spec 4)

hole_d   = 3.2;         // panel mounting holes
hole_end = 2.90;        // from each short end   (pitch 75.90)
hole_side= 2.60;        // from each long edge   (pitch 44.80)
pin_d    = 2.9;         // locating pins, 0.3 under the hole
pin_h    = 1.4;         // just under pcb_t, so nothing protrudes

rim_w    = 2.5;         // pocket rim
rim_h    = 3.0;
tab_w    = 10.0;        // snap tabs retaining the panel
tab_lip  = 1.2;         // how far each tab overhangs the PCB
tab_gap  = 0.15;        // clearance over the PCB back face
tab_slot = 1.0;         // relief either side, so the tab can flex

// ---------------------------------------------------------------- the DevKit
dk_l     = 51.7;
dk_w     = 28.5;
dk_hous  = 16.3;        // housing well below the PCB
dk_pcb_t = 1.6;
dk_back  = 0.5;         // gap behind the board, to the cover
usb_w    = 14.0;
usb_h    = 9.0;

// ---------------------------------------------------------------- the cover
cov_t    = 2.5;
cov_gap  = 0.30;
ledge    = 1.5;
vent_w   = 3.0;
vent_n   = 7;

// ------------------------------------------------------------------ derived
face_len  = case_h / cos(rake);              // face runs the full height
pocket_l  = pcb_l + pcb_gap;
pocket_w  = pcb_w + pcb_gap;
pocket_u0 = (case_w - pocket_l) / 2;
pocket_v0 = (face_len - pocket_w) / 2;
pcb_u0    = (case_w - pcb_l) / 2;
pcb_v0    = (face_len - pcb_w) / 2;

ap_l      = aa_l + 2 * ap_m;
ap_w      = aa_w + 2 * ap_m;
ap_u0     = (case_w - ap_l) / 2 - aa_off;    // image centred on the case
ap_v0     = (face_len - ap_w) / 2;

floor_z   = wall;
dk_z      = floor_z + dk_hous;               // DevKit PCB underside
dk_y1     = case_d - cov_t - dk_back;        // DevKit back edge, just off the cover
dk_y0     = dk_y1 - dk_w;
dk_x0     = (case_w - dk_l) / 2;

op_x0 = wall;            op_x1 = case_w - wall;    // back opening
op_z0 = wall;            op_z1 = case_h - wall;

// Face frame: local (u, v, w) with w measured OUT of the front face.
face_m = [[1, 0,          0,         0],
          [0, sin(rake), -cos(rake), 0],
          [0, cos(rake),  sin(rake), 0],
          [0, 0,          0,         1]];

module on_face() { multmatrix(face_m) children(); }

// ------------------------------------------------------------------ profiles
module outer_profile() {
    polygon([[0, 0],
             [case_d, 0],
             [case_d, case_h],
             [case_h * tan(rake), case_h]]);
}

module cavity_profile() {
    f = face_t / cos(rake);                  // horizontal run of the face wall
    polygon([[floor_z * tan(rake) + f,        floor_z],
             [case_d + 10,                    floor_z],
             [case_d + 10,                    op_z1],
             [op_z1 * tan(rake) + f,          op_z1]]);
}

// extrude a (y,z) profile along x
module extrude_yz(x0, x1) {
    translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(x1 - x0) children();
}

// ---------------------------------------------------------------------- body
module shell() { extrude_yz(0, case_w) outer_profile(); }
module cavity() { extrude_yz(op_x0, op_x1) cavity_profile(); }

module aperture() {
    on_face() translate([ap_u0, ap_v0, -face_t - 20])
        cube([ap_l, ap_w, face_t + 30]);
}

module pocket_rim() {
    // +ov fuses the rim into the face rather than merely touching it
    ov = 0.6;
    on_face() translate([0, 0, -face_t - rim_h]) difference() {
        translate([pocket_u0 - rim_w, pocket_v0 - rim_w, 0])
            cube([pocket_l + 2 * rim_w, pocket_w + 2 * rim_w, rim_h + ov]);
        translate([pocket_u0, pocket_v0, -1])
            cube([pocket_l, pocket_w, rim_h + ov + 2]);
    }
}

// Snap tabs on the two long pocket edges. Cross-section in (v, w): a flat
// retaining face just behind the PCB, and a 45 deg lead-in so the panel presses
// straight in. Relief slots either side let each tab flex.
tab_back = -(face_t + pcb_t + tab_gap);     // retaining face
tab_deep = -(face_t + rim_h);               // rim inner face

module tab_at(v_edge, dir) {                // dir = +1 lower edge, -1 upper
    root = v_edge - dir * 0.6;              // overlap into the rim, so it fuses
    polygon([[root,                    tab_back],
             [v_edge + dir * tab_lip,  tab_back],
             [root,                    tab_deep]]);
}

module panel_tabs() {
    for (u = [pocket_u0 + pocket_l * 0.25, pocket_u0 + pocket_l * 0.75])
        on_face() translate([u - tab_w / 2, 0, 0]) rotate([90, 0, 90])
            linear_extrude(tab_w) {
                tab_at(pocket_v0, 1);
                tab_at(pocket_v0 + pocket_w, -1);
            }
}

module tab_relief() {
    for (u = [pocket_u0 + pocket_l * 0.25, pocket_u0 + pocket_l * 0.75],
         du = [-tab_w / 2 - tab_slot, tab_w / 2],
         v  = [pocket_v0 - rim_w - 1, pocket_v0 + pocket_w])
        on_face() translate([u + du, v, tab_deep - 1])
            cube([tab_slot, rim_w + 1, rim_h + 2]);
}

module locating_pins() {
    for (du = [hole_side, pcb_l - hole_side], dv = [hole_end, pcb_w - hole_end])
        on_face() translate([pcb_u0 + du, pcb_v0 + dv, -face_t - pin_h])
            cylinder(d = pin_d, h = pin_h + 0.6);
}

module dk_ledges() {
    for (x = [dk_x0 - 5, dk_x0 + dk_l])
        translate([x, dk_y0, floor_z - 0.6]) cube([5, dk_w, dk_z - floor_z + 0.6]);
    // upstands locating the board in x
    for (x = [dk_x0 - 5, dk_x0 + dk_l + 3.5])
        translate([x, dk_y0, dk_z]) cube([1.5, dk_w, dk_pcb_t + 1.5]);
}

module usb_opening() {
    translate([-5, (dk_y0 + dk_y1) / 2 - usb_w / 2, dk_z + dk_pcb_t + 1.6 - usb_h / 2])
        cube([wall + 10, usb_w, usb_h]);
}

module cover_ledge() {
    difference() {
        translate([op_x0 - 0.6, case_d - cov_t - ledge, op_z0 - 0.6])
            cube([op_x1 - op_x0 + 1.2, ledge, op_z1 - op_z0 + 1.2]);
        translate([op_x0 + ledge, case_d - cov_t - ledge - 1, op_z0 + ledge])
            cube([op_x1 - op_x0 - 2 * ledge, ledge + 2, op_z1 - op_z0 - 2 * ledge]);
    }
}

module body() {
    union() {
        difference() {
            shell();
            cavity();
            aperture();
            usb_opening();
        }
        intersection() {
            difference() {
                union() { pocket_rim(); panel_tabs(); locating_pins();
                          dk_ledges(); cover_ledge(); }
                tab_relief();
            }
            extrude_yz(-1, case_w + 1) outer_profile();
        }
    }
}

// --------------------------------------------------------------------- cover
module cover() {
    cw = op_x1 - op_x0 - 2 * cov_gap;
    ch = op_z1 - op_z0 - 2 * cov_gap;
    difference() {
        translate([op_x0 + cov_gap, case_d - cov_t, op_z0 + cov_gap])
            cube([cw, cov_t, ch]);
        // vents, low and high
        for (i = [0 : vent_n - 1], zf = [0.14, 0.86])
            translate([op_x0 + cw * (0.18 + 0.64 * i / (vent_n - 1)) - vent_w / 2,
                       case_d - cov_t - 1, op_z0 + ch * zf - 6])
                cube([vent_w, cov_t + 2, 12]);
    }
    // snap tabs catching the inside of the ledge
    for (x = [op_x0 + cw * 0.25, op_x0 + cw * 0.75], z = [op_z0 + 2, op_z1 - 5])
        translate([x - 4, case_d - cov_t - ledge - 1.2, z])
            cube([8, ledge + 1.2, 3]);
}

// ---------------------------------------------------------------------- main
if (part == "body")  body();
else if (part == "cover") cover();
else { body(); translate([0, 25, 0]) cover(); }
